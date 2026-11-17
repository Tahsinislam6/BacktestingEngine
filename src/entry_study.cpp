#include "entry_study.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>

#include "control.hpp"
#include "indicators/atr.hpp"

namespace
{
    template <typename... Args>
    std::string fmt(const char* format, Args... args)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    std::string num(double v, const char* format = "%.2f") { return std::isnan(v) ? "n/a" : fmt(format, v); }
    std::string pct(double f) { return std::isnan(f) ? "n/a" : fmt("%.0f%%", 100.0 * f); }

    enum Outcome { Target = 0, Stop = 1, Neither = 2 };

    // One signal and everything measured about it, for the direction that was signalled (+1) and the other (-1)
    // where the measure depends on the direction.
    struct Event
    {
        int direction{};                          // +1 long, -1 short
        std::vector<double> move;                 // per horizon: (close h bars later - close) / atr, NaN if there is no such bar
        double costAtr{};                         // spread / atr
        Outcome raceLong{Neither}, raceShort{Neither};
        bool hasExcursion{false};
        double up{0}, down{0};                    // best move up / down over the next kExcursionBars bars, >= 0, in ATR
    };

    // Sums over a set of directions (the real ones, or one random draw).
    struct Tally
    {
        std::vector<double> sum;                  // per horizon: sum of direction * move
        std::vector<double> netSum;
        std::vector<int> count, positive;
        int target{0}, stop{0};
        int excursionCount{0};
        double mfe{0}, mae{0};

        explicit Tally(std::size_t horizons)
            : sum(horizons, 0.0), netSum(horizons, 0.0), count(horizons, 0), positive(horizons, 0) {}
    };

    Tally tally(const std::vector<Event>& events, const std::vector<int>& direction, std::size_t horizons)
    {
        Tally t(horizons);
        for (std::size_t e = 0; e < events.size(); ++e)
        {
            const Event& ev = events[e];
            const int d = direction[e];
            for (std::size_t h = 0; h < horizons; ++h)
            {
                if (std::isnan(ev.move[h]))
                    continue;
                const double signedMove = d * ev.move[h];
                t.sum[h] += signedMove;
                t.netSum[h] += signedMove - ev.costAtr;
                ++t.count[h];
                if (signedMove > 0)
                    ++t.positive[h];
            }
            const Outcome race = d > 0 ? ev.raceLong : ev.raceShort;
            if (race == Target) ++t.target;
            else if (race == Stop) ++t.stop;
            if (ev.hasExcursion)
            {
                ++t.excursionCount;
                t.mfe += d > 0 ? ev.up : ev.down;
                t.mae += d > 0 ? ev.down : ev.up;
            }
        }
        return t;
    }

    double mean(double sum, int count) { return count > 0 ? sum / count : std::nan(""); }

    double raceR(const Tally& t, int events, const EntryStudyOptions& o)
    {
        if (events == 0)
            return std::nan("");
        return (static_cast<double>(t.target) / events * o.targetAtr - static_cast<double>(t.stop) / events * o.stopAtr) / o.stopAtr;
    }
}

const char* signalKindName(SignalKind kind)
{
    switch (kind)
    {
        case SignalKind::Cross: return "cross";
        case SignalKind::Breakout: return "breakout";
        case SignalKind::Momentum: return "momentum";
    }
    return "";
}

const std::vector<int>& entryHorizons()
{
    static const std::vector<int> horizons{1, 2, 3, 5, 10, 20, 40};
    return horizons;
}

namespace
{
    void checkOptions(const EntryStudyOptions& options)
    {
        if (options.atrPeriod < 1)
            throw std::invalid_argument("atrPeriod must be at least 1");
        if (!(options.stopAtr > 0.0) || !(options.targetAtr > 0.0))
            throw std::invalid_argument("stopAtr and targetAtr must be positive");
        if (options.maxDistanceAtr && !(*options.maxDistanceAtr > 0.0))
            throw std::invalid_argument("maxDistanceAtr must be positive");
        if (options.confirmationWindow < 1)
            throw std::invalid_argument("confirmationWindow must be at least 1");
        if (options.kind != SignalKind::Cross && options.lookback < 1)
            throw std::invalid_argument("lookback must be at least 1");
    }

    struct Collected
    {
        std::vector<Event> events;
        int signals{0};
    };

    // The entries (see the header) and what happens after each of them.
    using LineMaker = std::function<std::unique_ptr<AnyLine>()>;        // empty for the signals without a baseline line

    Collected collect(const AlignedData& data, const LineMaker& makeLine, const EntryStudyOptions& options,
                      const std::vector<double>& spreads)
    {
        const std::vector<int>& horizons = entryHorizons();
        const std::size_t assets = data.symbols.size();
        const int window = options.components.confirmation ? options.confirmationWindow : 1;

        std::vector<std::vector<Bar>> series(assets);
        std::vector<std::unique_ptr<AnyLine>> lines;
        std::vector<std::unique_ptr<AnyState>> confirm;
        std::vector<std::unique_ptr<AnyGate>> gate;
        std::vector<Atr> atrs(assets, Atr(options.atrPeriod));
        std::vector<int> side(assets, 0), pendingDir(assets, 0), pendingLeft(assets, 0);
        // Breakout: the series index of the last break each way; Momentum: the last sign
        std::vector<long> lastUp(assets, -1000000000), lastDown(assets, -1000000000);
        std::vector<int> lastSign(assets, 0);
        for (std::size_t a = 0; a < assets; ++a)
        {
            lines.push_back(makeLine ? makeLine() : nullptr);
            if (options.components.confirmation)
                confirm.push_back(options.components.confirmation->clone());
            if (options.components.filter)
                gate.push_back(options.components.filter->clone());
        }

        Collected out;
        struct Raw { std::size_t asset; std::size_t index; int direction; double close; double atr; };
        std::vector<Raw> raw;
        for (const Snapshot& snapshot : data.snapshots)
        {
            for (std::size_t a = 0; a < assets && a < snapshot.bars.size(); ++a)
            {
                if (!snapshot.bars[a])
                    continue;
                const Bar& bar = *snapshot.bars[a];
                if (lines[a]) lines[a]->update(bar);
                atrs[a].update(bar);
                if (!confirm.empty()) confirm[a]->update(bar);
                if (!gate.empty()) gate[a]->update(bar);
                series[a].push_back(bar);
                const std::vector<Bar>& bars = series[a];
                const long here = static_cast<long>(bars.size()) - 1;
                const std::size_t lookback = static_cast<std::size_t>(std::max(options.lookback, 1));
                if (!atrs[a].ready() || (!confirm.empty() && !confirm[a]->ready()) || (!gate.empty() && !gate[a]->ready()))
                    continue;

                // the signal of this bar: +1, -1 or 0
                int signal = 0;
                double line = 0;
                if (options.kind == SignalKind::Cross)
                {
                    if (!lines[a]->ready())
                        continue;
                    line = lines[a]->value();
                    const int now = bar.close > line ? 1 : bar.close < line ? -1 : 0;
                    if (side[a] != 0 && now != 0 && now != side[a])
                        signal = now;
                    if (now != 0)
                        side[a] = now;
                }
                else if (bars.size() > lookback)
                {
                    if (options.kind == SignalKind::Breakout)
                    {
                        double high = bars[bars.size() - 1 - lookback].high, low = bars[bars.size() - 1 - lookback].low;
                        for (std::size_t j = bars.size() - lookback; j + 1 < bars.size(); ++j)
                        {
                            high = std::max(high, bars[j].high);
                            low = std::min(low, bars[j].low);
                        }
                        const int dir = bar.close > high ? 1 : bar.close < low ? -1 : 0;
                        long& last = dir > 0 ? lastUp[a] : lastDown[a];
                        if (dir != 0)
                        {
                            if (here - last > static_cast<long>(lookback))
                                signal = dir;
                            last = here;
                        }
                    }
                    else
                    {
                        const double change = bar.close - bars[bars.size() - 1 - lookback].close;
                        const int sign = change > 0 ? 1 : change < 0 ? -1 : 0;
                        if (sign != 0)
                        {
                            if (lastSign[a] != 0 && sign != lastSign[a])
                                signal = sign;
                            lastSign[a] = sign;
                        }
                    }
                }
                else
                    continue;

                if (signal != 0)
                {
                    ++out.signals;
                    pendingDir[a] = signal;
                    pendingLeft[a] = window;
                }
                if (pendingLeft[a] <= 0)
                    continue;

                const double atr = atrs[a].value();
                const bool passes = atr > 0.0 &&
                    (options.kind != SignalKind::Cross || !options.maxDistanceAtr ||
                     std::abs(bar.close - line) <= *options.maxDistanceAtr * atr) &&
                    (confirm.empty() || confirm[a]->state() == pendingDir[a]) &&
                    (gate.empty() || gate[a]->allows());
                if (passes)
                {
                    raw.push_back({a, series[a].size() - 1, pendingDir[a], bar.close, atr});
                    pendingLeft[a] = 0;
                }
                else
                    --pendingLeft[a];
            }
        }

        // ---- what happens after each entry ----
        out.events.reserve(raw.size());
        for (const Raw& r : raw)
        {
            const std::vector<Bar>& bars = series[r.asset];
            Event ev;
            ev.direction = r.direction;
            ev.costAtr = r.asset < spreads.size() ? spreads[r.asset] / r.atr : 0.0;

            for (int h : horizons)
            {
                const std::size_t at = r.index + static_cast<std::size_t>(h);
                ev.move.push_back(at < bars.size() ? (bars[at].close - r.close) / r.atr : std::nan(""));
            }

            const double longStop = r.close - options.stopAtr * r.atr, longTarget = r.close + options.targetAtr * r.atr;
            const double shortStop = r.close + options.stopAtr * r.atr, shortTarget = r.close - options.targetAtr * r.atr;
            bool longDone = false, shortDone = false;
            for (std::size_t j = r.index + 1; j < bars.size() && j <= r.index + kRaceBars && !(longDone && shortDone); ++j)
            {
                const Bar& b = bars[j];
                if (!longDone)
                {
                    if (b.low <= longStop) { ev.raceLong = Stop; longDone = true; }
                    else if (b.high >= longTarget) { ev.raceLong = Target; longDone = true; }
                }
                if (!shortDone)
                {
                    if (b.high >= shortStop) { ev.raceShort = Stop; shortDone = true; }
                    else if (b.low <= shortTarget) { ev.raceShort = Target; shortDone = true; }
                }
            }

            if (r.index + kExcursionBars < bars.size())
            {
                double high = r.close, low = r.close;
                for (std::size_t j = r.index + 1; j <= r.index + kExcursionBars; ++j)
                {
                    high = std::max(high, bars[j].high);
                    low = std::min(low, bars[j].low);
                }
                ev.hasExcursion = true;
                ev.up = (high - r.close) / r.atr;
                ev.down = (r.close - low) / r.atr;
            }
            out.events.push_back(std::move(ev));
        }
        return out;
    }
}

namespace
{
EntryStudyResult studyOf(const Collected& collected, const EntryStudyOptions& options, int randomRuns)
{
    const std::vector<int>& horizons = entryHorizons();
    const std::vector<Event>& events = collected.events;

    // ---- the real directions, then the random ones ----
    EntryStudyResult result;
    result.signals = collected.signals;
    result.events = static_cast<int>(events.size());
    result.stopAtr = options.stopAtr;
    result.targetAtr = options.targetAtr;

    std::vector<int> real(events.size());
    for (std::size_t e = 0; e < events.size(); ++e)
        real[e] = events[e].direction;
    const Tally realTally = tally(events, real, horizons.size());

    std::vector<std::vector<double>> randomMeans(horizons.size());
    std::vector<double> randomTarget, randomRace, randomBalance;
    double randomMfe = 0, randomMae = 0;
    int randomExcursionRuns = 0;
    for (int run = 1; run <= randomRuns; ++run)
    {
        std::mt19937_64 rng(static_cast<std::uint64_t>(run));
        std::vector<int> direction(events.size());
        for (int& d : direction)
            d = (rng() >> 63) ? 1 : -1;
        const Tally t = tally(events, direction, horizons.size());
        for (std::size_t h = 0; h < horizons.size(); ++h)
            randomMeans[h].push_back(mean(t.sum[h], t.count[h]));
        randomTarget.push_back(events.empty() ? std::nan("") : static_cast<double>(t.target) / events.size());
        randomRace.push_back(raceR(t, result.events, options));
        if (t.excursionCount > 0)
        {
            randomBalance.push_back((t.mfe - t.mae) / t.excursionCount);
            randomMfe += t.mfe / t.excursionCount;
            randomMae += t.mae / t.excursionCount;
            ++randomExcursionRuns;
        }
    }

    auto average = [](const std::vector<double>& v)
    {
        double sum = 0;
        int n = 0;
        for (double x : v)
            if (std::isfinite(x)) { sum += x; ++n; }
        return n > 0 ? sum / n : std::nan("");
    };

    for (std::size_t h = 0; h < horizons.size(); ++h)
    {
        HorizonResult r;
        r.horizon = horizons[h];
        r.events = realTally.count[h];
        r.meanAtr = mean(realTally.sum[h], realTally.count[h]);
        r.netMeanAtr = mean(realTally.netSum[h], realTally.count[h]);
        r.shareUp = realTally.count[h] > 0 ? static_cast<double>(realTally.positive[h]) / realTally.count[h] : std::nan("");
        r.randomMeanAtr = average(randomMeans[h]);
        r.beats = percentileRank(r.meanAtr, randomMeans[h]);
        result.horizons.push_back(r);
    }

    if (result.events > 0)
    {
        result.targetFirst = static_cast<double>(realTally.target) / result.events;
        result.stopFirst = static_cast<double>(realTally.stop) / result.events;
        result.neither = static_cast<double>(result.events - realTally.target - realTally.stop) / result.events;
        result.raceR = raceR(realTally, result.events, options);
        result.randomTargetFirst = average(randomTarget);
        result.randomRaceR = average(randomRace);
        result.targetBeats = percentileRank(result.targetFirst, randomTarget);
    }

    result.excursionEvents = realTally.excursionCount;
    if (realTally.excursionCount > 0)
    {
        result.mfeAtr = realTally.mfe / realTally.excursionCount;
        result.maeAtr = realTally.mae / realTally.excursionCount;
        if (randomExcursionRuns > 0)
        {
            result.randomMfeAtr = randomMfe / randomExcursionRuns;
            result.randomMaeAtr = randomMae / randomExcursionRuns;
        }
        result.excursionBeats = percentileRank(result.mfeAtr - result.maeAtr, randomBalance);
    }
    return result;
}
}

EntryStudyResult runEntryStudy(const AlignedData& data, const BaselineInfo& baseline, const Params& params,
                               const EntryStudyOptions& options, int randomRuns, const std::vector<double>& spreads)
{
    if (randomRuns < 1)
        throw std::invalid_argument("the entry study needs at least one random run");
    checkOptions(options);

    if (options.kind != SignalKind::Cross)
        throw std::invalid_argument("runEntryStudy is for baseline crosses; use runSignalStudy for other signals");
    return studyOf(collect(data, [&] { return baseline.makeLine(params); }, options, spreads), options, randomRuns);
}

EntryStudyResult runSignalStudy(const AlignedData& data, const EntryStudyOptions& options, int randomRuns,
                                const std::vector<double>& spreads)
{
    if (randomRuns < 1)
        throw std::invalid_argument("the entry study needs at least one random run");
    checkOptions(options);
    if (options.kind == SignalKind::Cross)
        throw std::invalid_argument("runSignalStudy is for breakouts and momentum; use runEntryStudy for baseline crosses");
    return studyOf(collect(data, LineMaker{}, options, spreads), options, randomRuns);
}

std::string formatEntryStudy(const std::string& label, const EntryStudyResult& r)
{
    std::string out = "  ENTRY STUDY, " + label + ": " + std::to_string(r.events) + " entries from " +
                      std::to_string(r.signals) + " baseline crosses (the don't-chase filter and any components decide), measured in ATR\n";
    out += "  the move after the signal, in the signal's direction; 'beats' = share of random-direction runs it is better than\n";
    out += fmt("  %5s %7s %9s %9s %6s %11s %7s\n", "bars", "events", "mean ATR", "net ATR", "% up", "random mean", "beats");
    for (const HorizonResult& h : r.horizons)
        out += fmt("  %5d %7d %9s %9s %6s %11s %7s\n", h.horizon, h.events, num(h.meanAtr, "%.3f").c_str(),
                   num(h.netMeanAtr, "%.3f").c_str(), pct(h.shareUp).c_str(), num(h.randomMeanAtr, "%.3f").c_str(),
                   pct(h.beats).c_str());

    out += fmt("  race, target %.1f ATR vs stop %.1f ATR over %d bars: target first %s, stop first %s, neither %s\n",
               r.targetAtr, r.stopAtr, kRaceBars, pct(r.targetFirst).c_str(), pct(r.stopFirst).c_str(), pct(r.neither).c_str());
    out += fmt("    random directions: target first %s; this beats %s of them; race R %s (random %s, before costs)\n",
               pct(r.randomTargetFirst).c_str(), pct(r.targetBeats).c_str(), num(r.raceR, "%.3f").c_str(),
               num(r.randomRaceR, "%.3f").c_str());
    out += fmt("  excursions over %d bars (%d signals): favourable %s ATR, adverse %s ATR; random %s / %s; "
               "the balance beats %s of random runs\n",
               kExcursionBars, r.excursionEvents, num(r.mfeAtr).c_str(), num(r.maeAtr).c_str(),
               num(r.randomMfeAtr).c_str(), num(r.randomMaeAtr).c_str(), pct(r.excursionBeats).c_str());
    return out;
}

namespace
{
    EntryArm armOf(const Collected& collected, const EntryStudyOptions& options)
    {
        const std::vector<Event>& events = collected.events;
        std::vector<int> real(events.size());
        for (std::size_t e = 0; e < events.size(); ++e)
            real[e] = events[e].direction;
        const std::size_t horizons = entryHorizons().size();
        const Tally t = tally(events, real, horizons);

        EntryArm arm;
        arm.events = static_cast<int>(events.size());
        for (std::size_t h = 0; h < horizons; ++h)
            arm.meanAtr.push_back(mean(t.sum[h], t.count[h]));
        arm.raceR = raceR(t, arm.events, options);
        if (t.excursionCount > 0)
            arm.balance = (t.mfe - t.mae) / t.excursionCount;
        return arm;
    }

    std::vector<double> meanColumn(const std::vector<EntryArm>& arms, std::size_t h)
    {
        std::vector<double> out;
        for (const EntryArm& a : arms)
            out.push_back(a.meanAtr[h]);
        return out;
    }

    // mean and sample sd of the finite values
    void meanSd(const std::vector<double>& v, double& m, double& sd)
    {
        int n = 0;
        double sum = 0;
        for (double x : v)
            if (std::isfinite(x)) { sum += x; ++n; }
        m = n > 0 ? sum / n : std::nan("");
        sd = std::nan("");
        if (n > 1)
        {
            double sq = 0;
            for (double x : v)
                if (std::isfinite(x)) sq += (x - m) * (x - m);
            sd = std::sqrt(sq / (n - 1));
        }
    }
}

EntryControlResult runEntryComponentControl(const AlignedData& data, const BaselineInfo& baseline, const Params& params,
                                            const EntryStudyOptions& options, ComponentRole role, int seeds,
                                            const std::vector<double>& spreads)
{
    if (seeds < 1)
        throw std::invalid_argument("the entry control needs at least one random run");
    if (role == ComponentRole::ExitIndicator)
        throw std::invalid_argument("an exit indicator does not pick entries: the entry control takes a confirmation or a filter");
    if (!hasRole(options.components, role))
        throw std::invalid_argument(std::string("there is no ") + roleName(role) + " to control");
    checkOptions(options);
    if (options.kind != SignalKind::Cross)
        throw std::invalid_argument("the entry control is for baseline crosses");

    const StandIn stand = measureStandIn(options.components, role, data);
    const LineMaker makeLine = [&] { return baseline.makeLine(params); };

    EntryControlResult out;
    out.behaviour = stand.behaviour;
    out.real = armOf(collect(data, makeLine, options, spreads), options);

    EntryStudyOptions bare = options;
    bare.components = withoutRole(options.components, role);
    out.without = armOf(collect(data, makeLine, bare, spreads), bare);

    for (int seed = 1; seed <= seeds; ++seed)
    {
        EntryStudyOptions replaced = options;
        replaced.components = withStandIn(options.components, role, stand, static_cast<std::uint64_t>(seed));
        out.random.push_back(armOf(collect(data, makeLine, replaced, spreads), replaced));
    }

    for (std::size_t h = 0; h < entryHorizons().size(); ++h)
        out.meanBeats.push_back(percentileRank(out.real.meanAtr[h], meanColumn(out.random, h)));
    std::vector<double> races, balances;
    for (const EntryArm& a : out.random)
    {
        races.push_back(a.raceR);
        balances.push_back(a.balance);
    }
    out.raceBeats = percentileRank(out.real.raceR, races);
    out.balanceBeats = percentileRank(out.real.balance, balances);
    return out;
}

std::string formatEntryControl(const std::string& label, const std::string& componentName, ComponentRole role,
                               const EntryControlResult& r)
{
    std::string out = fmt("  ENTRY CONTROL, %s: %s '%s' vs %zu random %ss that behave the same\n", label.c_str(),
                          roleName(role), componentName.c_str(), r.random.size(), roleName(role));
    out += "  (matched: " + r.behaviour + ")\n";

    double meanEvents = 0;
    for (const EntryArm& a : r.random)
        meanEvents += a.events;
    meanEvents /= std::max<std::size_t>(1, r.random.size());
    out += fmt("  entries taken: with it %d, without %d, random mean %.0f\n", r.real.events, r.without.events, meanEvents);
    out += "  the move after the entry in the signal's direction, in ATR; 'it beats' = share of random components it is above\n";
    out += fmt("  %5s %9s %9s %11s %7s %8s\n", "bars", "with it", "without", "random mean", "sd", "it beats");
    for (std::size_t h = 0; h < r.real.meanAtr.size(); ++h)
    {
        double m, sd;
        meanSd(meanColumn(r.random, h), m, sd);
        out += fmt("  %5d %9s %9s %11s %7s %8s\n", entryHorizons()[h], num(r.real.meanAtr[h], "%.3f").c_str(),
                   num(r.without.meanAtr[h], "%.3f").c_str(), num(m, "%.3f").c_str(), num(sd, "%.3f").c_str(),
                   pct(r.meanBeats[h]).c_str());
    }
    std::vector<double> races, balances;
    for (const EntryArm& a : r.random)
    {
        races.push_back(a.raceR);
        balances.push_back(a.balance);
    }
    double rm, rsd, bm, bsd;
    meanSd(races, rm, rsd);
    meanSd(balances, bm, bsd);
    out += fmt("  race R (target vs stop, before costs): with it %s, without %s, random mean %s; it beats %s\n",
               num(r.real.raceR, "%.3f").c_str(), num(r.without.raceR, "%.3f").c_str(), num(rm, "%.3f").c_str(),
               pct(r.raceBeats).c_str());
    out += fmt("  excursion balance (favourable - adverse ATR over %d bars): with it %s, without %s, random mean %s; it beats %s\n",
               kExcursionBars, num(r.real.balance).c_str(), num(r.without.balance).c_str(), num(bm).c_str(),
               pct(r.balanceBeats).c_str());
    return out;
}
