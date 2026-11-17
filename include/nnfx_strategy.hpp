#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "baseline.hpp"
#include "components.hpp"
#include "indicators/atr.hpp"
#include "multi_backtester.hpp"
#include "nnfx_orders.hpp"

// How NnfxStrategy trades.
struct NnfxOptions
{
    NnfxEntryRules entry;                       // stop, target and risk per half (see nnfx_orders.hpp)
    std::optional<double> maxDistanceAtr{1.0};  // skip an entry whose close is further than this many ATR from
                                                // the line; nullopt = take every signal
    bool exitOnCross{true};                     // close the trades when the close crosses back over the line
    bool breakeven{true};                       // after the target is hit, move the runner's stop to its entry price
    std::optional<double> trailAtr{1.5};        // after the target, trail the runner's stop this many ATR behind the
                                                // best close since entry; nullopt = no trailing stop
    int atrPeriod{14};

    // Other NNFX components (each may be empty) and how they are used:
    NnfxComponents components;
    int confirmationWindow{7};                  // bars a signal waits for its confirmation, the cross bar included
    bool oneCandleRule{false};                  // a signal that cannot be taken on the cross bar gets one more bar
    bool continuation{false};                   // re-enter after the trades are gone while price stays on the same
                                                // side of the line (and the other checks pass)

    // Portfolio limits (nullopt = none), counted over all assets:
    std::optional<double> maxOpenRisk;          // total risk of open signals as a share of equity (a signal = 2 x riskPerHalf)
    std::optional<int> maxPerCurrency;          // net signals per currency, e.g. long EURUSD and long GBPUSD are both
                                                // -1 USD; needs the asset names to look like "EURUSD"

    // CONTROL: when set, the signal timing is still the baseline cross but every entry's direction is a coin
    // flip (seeded, so repeatable), to see whether the baseline's direction adds anything over luck.
    // Needs exitOnCross == false. The other checks (confirmation, filter, limits) use the baseline's direction.
    std::optional<std::uint64_t> randomSeed;
};

// The NNFX entry and trade management, one signal at a time per asset:
//
//   Signal     the close crosses the baseline line. The first bar after warm-up never signals, and a close
//              exactly on the line is no signal (the previous side is kept). A signal stays pending for a
//              window of bars (1; 2 with oneCandleRule; confirmationWindow when there is a confirmation).
//   Entry      on the first bar of the window where every check passes, two trades (nnfxEntryOrders): a half with
//              a take-profit and a runner, one shared stop. Checks: the asset has no open trades; the close is no
//              further than maxDistanceAtr * ATR from the line; the confirmation's state matches the direction; the
//              filter allows; the portfolio limits hold. A signal that never passes is lost.
//   Continuation (optional) with no pending signal, the asset quiet for two bars (no trades open, none entered or
//              closed), the price still on the side of the last signal, and the same checks: enter again. A cross
//              over the line forgets the last signal.
//   Cross back exitOnCross: both trades are closed at the close, and the new signal may enter the other way.
//   Exit indicator an open trade is closed when its state is the opposite of the trade's direction.
//   Breakeven  once the take-profit half is gone and the runner is still open (so the target was hit, not
//              the stop), the runner's stop is moved to its entry price.
//   Trailing   such a runner's stop also follows the best close since entry, trailAtr * ATR behind it (the current
//              ATR); the stop used is the better of the two and it never moves back. A hit is recorded as Breakeven
//              while the entry price is the better level (a tie too) and as Trailing once the trail is.
//              These are real stop orders (OrderType::MoveStop, set at the close, first applying on the next bar): they
//              fill at the stop price, or at the open if the bar gaps through it. A level that is not on the right side
//              of the close cannot be placed, so the runner is closed at the close instead, with the same reason.
//
// B is any BaselineLine; it is copied once per asset.
template <BaselineLine B>
class NnfxStrategy : public MultiStrategy
{
public:
    NnfxStrategy(std::size_t assets, const B& prototype, const NnfxOptions& options = NnfxOptions{})
        : options_(options), lines_(assets, prototype), atrs_(assets, Atr(checkedPeriod(options))),
          assets_(assets), side_(assets, 0), best_(assets, 0.0), tracking_(assets, false),
          pendingDir_(assets, 0), pendingLeft_(assets, 0), lastDir_(assets, 0), cooldown_(assets, 0),
          currencies_(assets), rng_(options.randomSeed.value_or(0))
    {
        validateNnfxRules(options.entry);
        if (options.maxDistanceAtr && !(*options.maxDistanceAtr > 0.0))
            throw std::invalid_argument("maxDistanceAtr must be positive");
        if (options.trailAtr && !(*options.trailAtr > 0.0))
            throw std::invalid_argument("trailAtr must be positive");
        if (options.randomSeed && options.exitOnCross)
            throw std::invalid_argument("a random direction cannot be combined with the cross-back exit");
        if (options.confirmationWindow < 1)
            throw std::invalid_argument("confirmationWindow must be at least 1");
        if (options.maxOpenRisk && !(*options.maxOpenRisk > 0.0))
            throw std::invalid_argument("maxOpenRisk must be positive");
        if (options.maxPerCurrency && *options.maxPerCurrency < 1)
            throw std::invalid_argument("maxPerCurrency must be at least 1");

        for (std::size_t a = 0; a < assets; ++a)
        {
            if (options.components.confirmation)
                confirm_.push_back(options.components.confirmation->clone());
            if (options.components.exitIndicator)
                exit_.push_back(options.components.exitIndicator->clone());
            if (options.components.filter)
                gate_.push_back(options.components.filter->clone());
        }
    }

    void setSymbols(const std::vector<std::string>& symbols) override
    {
        for (std::size_t a = 0; a < assets_ && a < symbols.size(); ++a)
        {
            if (symbols[a].size() == 6)
                currencies_[a] = {symbols[a].substr(0, 3), symbols[a].substr(3, 3)};
        }
    }

    std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) override
    {
        lines_.update(snapshot);
        atrs_.update(snapshot);
        for (std::size_t a = 0; a < assets_ && a < snapshot.bars.size(); ++a)
        {
            if (!snapshot.bars[a])
                continue;
            if (!confirm_.empty()) confirm_[a]->update(*snapshot.bars[a]);
            if (!exit_.empty()) exit_[a]->update(*snapshot.bars[a]);
            if (!gate_.empty()) gate_[a]->update(*snapshot.bars[a]);
        }

        // what is open now, for the portfolio limits (kept up to date as this snapshot's orders are made)
        double openRisk = 0.0;
        std::map<std::string, int> exposure;
        for (std::size_t a = 0; a < assets_; ++a)
        {
            for (const Trade& t : portfolio.openTrades())
            {
                if (t.asset == static_cast<int>(a))
                    openRisk += riskOf(t);
            }
            if (const Trade* first = firstTrade(portfolio, static_cast<int>(a)))
                addExposure(exposure, a, first->direction == Direction::Long ? 1 : -1);
        }

        std::vector<Order> orders;
        for (std::size_t a = 0; a < assets_ && a < snapshot.bars.size(); ++a)
        {
            if (!snapshot.bars[a] || !lines_[a].ready() || !atrs_[a].ready() || !componentsReady(a))
                continue;

            const double close = snapshot.bars[a]->close;
            const double line = lines_[a].value();
            const int now = close > line ? 1 : close < line ? -1 : 0;
            const bool crossed = side_[a] != 0 && now != 0 && now != side_[a];
            if (now != 0)
                side_[a] = now;

            const int asset = static_cast<int>(a);
            std::vector<const Trade*> open;
            for (const Trade& t : portfolio.openTrades())
            {
                if (t.asset == asset)
                    open.push_back(&t);
            }
            const bool hadOpen = !open.empty();
            std::size_t remaining = open.size();

            // the best close since entry (highest for a long, lowest for a short), for the trailing exit
            if (open.empty())
                tracking_[a] = false;
            else
            {
                const bool isLong = open.front()->direction == Direction::Long;
                if (!tracking_[a])
                {
                    best_[a] = open.front()->entryPrice;
                    tracking_[a] = true;
                }
                best_[a] = isLong ? std::max(best_[a], close) : std::min(best_[a], close);
            }

            // ---- exits ----
            std::vector<int> closing;
            auto closeTrade = [&](const Trade* t, ExitReason why)
            {
                if (std::find(closing.begin(), closing.end(), t->id) != closing.end())
                    return;
                closing.push_back(t->id);
                orders.push_back(closeOrder(t->id, why));
                --remaining;
            };

            if (options_.exitOnCross && crossed)
            {
                for (const Trade* t : open)
                {
                    if ((t->direction == Direction::Long) != (now > 0))
                        closeTrade(t, ExitReason::Signal);
                }
            }
            else if (const Trade* runner = armedRunner(open))
            {
                const bool isLong = runner->direction == Direction::Long;

                // the stop the runner should have now: the entry price (breakeven) and/or the trail behind the best
                // close, whichever protects more; a tie counts as breakeven
                std::optional<double> level;
                ExitReason reason = ExitReason::Breakeven;
                if (options_.breakeven)
                    level = runner->entryPrice;
                if (options_.trailAtr)
                {
                    const double trail = *options_.trailAtr * atrs_[a].value();
                    const double trailLevel = isLong ? best_[a] - trail : best_[a] + trail;
                    if (!level || (isLong ? trailLevel > *level : trailLevel < *level))
                    {
                        level = trailLevel;
                        reason = ExitReason::Trailing;
                    }
                }

                if (level)
                {
                    const bool better = !runner->stopLoss || (isLong ? *level > *runner->stopLoss : *level < *runner->stopLoss);
                    const bool placeable = isLong ? *level < close : *level > close;
                    if (better && placeable)
                        orders.push_back(moveStopOrder(runner->id, *level, reason));
                    else if (better)
                        closeTrade(runner, reason);          // the stop would be on the wrong side of the price
                }
            }

            if (!exit_.empty() && !open.empty())
            {
                const int state = exit_[a]->state();
                for (const Trade* t : open)
                {
                    const int sign = t->direction == Direction::Long ? 1 : -1;
                    if (state == -sign)
                        closeTrade(t, ExitReason::Indicator);
                }
            }

            if (remaining == 0 && hadOpen)
            {
                for (const Trade* t : open)
                    openRisk -= riskOf(*t);
                addExposure(exposure, a, open.front()->direction == Direction::Long ? -1 : 1);
            }

            // ---- the signal ----
            if (crossed)
            {
                lastDir_[a] = 0;
                pendingDir_[a] = now;
                pendingLeft_[a] = std::max(options_.components.confirmation ? options_.confirmationWindow : 1,
                                           options_.oneCandleRule ? 2 : 1);
            }
            else if (pendingLeft_[a] > 0 && now != 0 && now != pendingDir_[a])
                pendingLeft_[a] = 0;

            // a continuation entry waits until the asset has been quiet for two bars: no trades open, entered or closed
            bool continuationOk = false;
            if (hadOpen || !closing.empty())
                cooldown_[a] = 2;
            else if (cooldown_[a] > 0)
                --cooldown_[a];
            else
                continuationOk = true;

            int want = 0;
            bool fromPending = false;
            if (remaining == 0)
            {
                if (pendingLeft_[a] > 0)
                {
                    want = pendingDir_[a];
                    fromPending = true;
                }
                else if (options_.continuation && continuationOk && lastDir_[a] != 0 && now == lastDir_[a])
                    want = lastDir_[a];
            }

            // ---- the entry ----
            bool entered = false;
            if (want != 0)
            {
                const double atr = atrs_[a].value();
                const double equity = portfolio.equity();
                const double newRisk = 2.0 * options_.entry.riskPerHalf * equity;
                const bool buy = want > 0;

                if (atr > 0.0 && equity > 0.0 && passesChecks(a, buy, close, line, atr, equity, newRisk, openRisk, exposure))
                {
                    Direction direction = buy ? Direction::Long : Direction::Short;
                    if (options_.randomSeed)
                        direction = (rng_() >> 63) ? Direction::Long : Direction::Short;

                    tracking_[a] = false;          // the new trades start their own best close
                    for (const Order& o : nnfxEntryOrders(direction, close, atr, equity, asset, options_.entry))
                        orders.push_back(o);

                    openRisk += newRisk;
                    addExposure(exposure, a, direction == Direction::Long ? 1 : -1);
                    lastDir_[a] = want;
                    pendingLeft_[a] = 0;
                    cooldown_[a] = 2;
                    entered = true;
                }
            }
            if (!entered && pendingLeft_[a] > 0 && (fromPending || remaining != 0))
                --pendingLeft_[a];
        }
        return orders;
    }

private:
    static int checkedPeriod(const NnfxOptions& o)
    {
        if (o.atrPeriod <= 0)
            throw std::invalid_argument("atrPeriod must be positive");
        return o.atrPeriod;
    }

    bool componentsReady(std::size_t a) const
    {
        return (confirm_.empty() || confirm_[a]->ready()) && (exit_.empty() || exit_[a]->ready()) &&
               (gate_.empty() || gate_[a]->ready());
    }

    // what the trade can still lose: the distance from its entry to its stop on the losing side. A stop moved to or
    // beyond the entry price has locked in a profit, so it risks nothing.
    static double riskOf(const Trade& t)
    {
        if (!t.stopLoss)
            return 0.0;
        const double distance = t.direction == Direction::Long ? t.entryPrice - *t.stopLoss : *t.stopLoss - t.entryPrice;
        return distance > 0.0 ? t.quantity * distance : 0.0;
    }

    static const Trade* firstTrade(const Portfolio& portfolio, int asset)
    {
        for (const Trade& t : portfolio.openTrades())
        {
            if (t.asset == asset)
                return &t;
        }
        return nullptr;
    }

    // +1 = long the base currency and short the quote
    void addExposure(std::map<std::string, int>& exposure, std::size_t a, int direction) const
    {
        if (currencies_[a].first.empty())
            return;
        exposure[currencies_[a].first] += direction;
        exposure[currencies_[a].second] -= direction;
    }

    bool passesChecks(std::size_t a, bool buy, double close, double line, double atr, double equity, double newRisk,
                      double openRisk, const std::map<std::string, int>& exposure) const
    {
        if (options_.maxDistanceAtr && std::abs(close - line) > *options_.maxDistanceAtr * atr)
            return false;
        if (!confirm_.empty() && confirm_[a]->state() != (buy ? 1 : -1))
            return false;
        if (!gate_.empty() && !gate_[a]->allows())
            return false;
        if (options_.maxOpenRisk && openRisk + newRisk > *options_.maxOpenRisk * equity + 1e-9)
            return false;
        if (options_.maxPerCurrency && !currencies_[a].first.empty())
        {
            const int d = buy ? 1 : -1;
            auto has = [&](const std::string& c) { auto it = exposure.find(c); return it == exposure.end() ? 0 : it->second; };
            if (std::abs(has(currencies_[a].first) + d) > *options_.maxPerCurrency ||
                std::abs(has(currencies_[a].second) - d) > *options_.maxPerCurrency)
                return false;
        }
        return true;
    }

    // The runner (no take-profit) of a signal whose take-profit half is no longer open, or nullptr.
    static const Trade* armedRunner(const std::vector<const Trade*>& open)
    {
        for (const Trade* runner : open)
        {
            if (runner->takeProfit)
                continue;
            bool siblingOpen = false;
            for (const Trade* t : open)
            {
                if (t->takeProfit && t->direction == runner->direction && t->entryTime == runner->entryTime)
                    siblingOpen = true;
            }
            if (!siblingOpen)
                return runner;
        }
        return nullptr;
    }

    NnfxOptions options_;
    PerAsset<B> lines_;
    PerAsset<Atr> atrs_;
    std::size_t assets_;
    std::vector<int> side_;      // +1 above the line, -1 below, 0 = not seen yet
    std::vector<double> best_;   // best close since entry, per asset (valid while tracking_)
    std::vector<bool> tracking_;
    std::vector<int> pendingDir_;     // direction of the signal waiting to be taken
    std::vector<int> pendingLeft_;    // bars it has left
    std::vector<int> lastDir_;        // direction of the last signal taken, forgotten at the next cross
    std::vector<int> cooldown_;       // quiet bars still needed before a continuation entry
    std::vector<std::pair<std::string, std::string>> currencies_;     // base and quote of each asset, if known
    std::vector<std::unique_ptr<AnyState>> confirm_;
    std::vector<std::unique_ptr<AnyState>> exit_;
    std::vector<std::unique_ptr<AnyGate>> gate_;
    std::mt19937_64 rng_;        // only used for the random-direction control
};
