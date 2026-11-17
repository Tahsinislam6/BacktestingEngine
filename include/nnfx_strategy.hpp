#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

#include "baseline.hpp"
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
    bool breakeven{true};                       // after the target is hit, close the runner if the close
                                                // returns to its entry price
    std::optional<double> trailAtr{1.5};        // after the target, close the runner when its close falls this many
                                                // ATR from the best close since entry; nullopt = no trailing exit
    int atrPeriod{14};

    // CONTROL: when set, the signal timing is still the baseline cross but every entry's direction is a coin
    // flip (seeded, so repeatable), to see whether the baseline's direction adds anything over luck.
    // Needs exitOnCross == false.
    std::optional<std::uint64_t> randomSeed;
};

// The baseline-only version of the NNFX entry and trade management, one signal at a time per asset:
//
//   Signal     the close crosses the baseline line. The first bar after warm-up never signals, and a close
//              exactly on the line is no signal (the previous side is kept).
//   Entry      two trades (nnfxEntryOrders): a half with a take-profit and a runner, one shared stop.
//              Only taken when the asset has no open trades, and skipped if the close is further than
//              maxDistanceAtr * ATR from the line. A skipped signal is not taken later.
//   Cross back exitOnCross: both trades are closed at the close, and the new signal may enter the other way.
//   Breakeven  once the take-profit half is gone and the runner is still open (so the target was hit, not
//              the stop), the runner is closed when the close is back at or beyond its entry price.
//   Trailing   such a runner is also closed when its close falls trailAtr * ATR back from the best close since
//              entry. Breakeven wins if both apply.
//              Both are checked on the close, because the engine cannot move a stop.
//
// B is any BaselineLine; it is copied once per asset.
template <BaselineLine B>
class NnfxStrategy : public MultiStrategy
{
public:
    NnfxStrategy(std::size_t assets, const B& prototype, const NnfxOptions& options = NnfxOptions{})
        : options_(options), lines_(assets, prototype), atrs_(assets, Atr(checkedPeriod(options))),
          assets_(assets), side_(assets, 0), best_(assets, 0.0), tracking_(assets, false), rng_(options.randomSeed.value_or(0))
    {
        validateNnfxRules(options.entry);
        if (options.maxDistanceAtr && !(*options.maxDistanceAtr > 0.0))
            throw std::invalid_argument("maxDistanceAtr must be positive");
        if (options.trailAtr && !(*options.trailAtr > 0.0))
            throw std::invalid_argument("trailAtr must be positive");
        if (options.randomSeed && options.exitOnCross)
            throw std::invalid_argument("a random direction cannot be combined with the cross-back exit");
    }

    std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) override
    {
        lines_.update(snapshot);
        atrs_.update(snapshot);

        std::vector<Order> orders;
        for (std::size_t a = 0; a < assets_ && a < snapshot.bars.size(); ++a)
        {
            if (!snapshot.bars[a] || !lines_[a].ready() || !atrs_[a].ready())
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

            // exits
            if (options_.exitOnCross && crossed)
            {
                for (const Trade* t : open)
                {
                    if ((t->direction == Direction::Long) != (now > 0))
                    {
                        orders.push_back(closeOrder(t->id));
                        --remaining;
                    }
                }
            }
            else if (const Trade* runner = armedRunner(open))
            {
                const bool isLong = runner->direction == Direction::Long;
                const bool backAtEntry = isLong ? close <= runner->entryPrice : close >= runner->entryPrice;
                const double trail = options_.trailAtr ? *options_.trailAtr * atrs_[a].value() : 0.0;
                const bool trailedOut = options_.trailAtr && (isLong ? close <= best_[a] - trail
                                                                      : close >= best_[a] + trail);
                if (options_.breakeven && backAtEntry)
                {
                    orders.push_back(closeOrder(runner->id, ExitReason::Breakeven));
                    --remaining;
                }
                else if (trailedOut)
                {
                    orders.push_back(closeOrder(runner->id, ExitReason::Trailing));
                    --remaining;
                }
            }

            // entry
            if (!crossed || remaining != 0)
                continue;
            const double atr = atrs_[a].value();
            const double equity = portfolio.equity();
            if (!(atr > 0.0) || !(equity > 0.0))
                continue;                 // cannot be sized: skip the signal rather than stop the run
            if (options_.maxDistanceAtr && std::abs(close - line) > *options_.maxDistanceAtr * atr)
                continue;

            Direction want = now > 0 ? Direction::Long : Direction::Short;
            if (options_.randomSeed)
                want = (rng_() >> 63) ? Direction::Long : Direction::Short;
            tracking_[a] = false;          // the new trades start their own best close
            for (const Order& o : nnfxEntryOrders(want, close, atr, equity, asset, options_.entry))
                orders.push_back(o);
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
    std::mt19937_64 rng_;        // only used for the random-direction control
};
