#include "grid.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "components_registry.hpp"
#include "param_space.hpp"
#include "report.hpp"

namespace
{
    template <typename... Args>
    std::string fmt(const char* format, Args... args)
    {
        char buffer[1024];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    std::string num(double v, const char* format = "%.2f") { return std::isnan(v) ? "nan" : fmt(format, v); }
    std::string pct(double f) { return std::isnan(f) ? "n/a" : fmt("%.0f%%", 100.0 * f); }

    // what one pair leaves behind besides its row: the options it ran with (for the holdout) and its random scores
    struct Pair
    {
        GridRow row;
        NnfxOptions nnfx;
        std::vector<double> luck;
        std::size_t baseline{}, c1{};
    };

    Pair runPair(const GridBaseline& b, const GridC1& c, const AlignedData& inSample, const AlignedData& outOfSample,
                 const GridOptions& options)
    {
        Pair p;
        p.row.baseline = b.info.name;
        p.row.params = paramsText(b.params);
        p.row.c1 = c.name;
        p.nnfx = options.nnfx;
        p.nnfx.components.confirmation = c.prototype;
        try
        {
            const StrategyFactory factory = b.info.nnfx(p.nnfx);
            const ArmResult in = runArm(inSample, b.params, factory, options.run);
            p.row.inSampleTrades = in.trades;
            p.row.inSampleExpectancyR = in.expectancyR;

            const ControlResult out = runDirectionControl(outOfSample, b.params, b.info.nnfx, p.nnfx, options.controlRuns, options.run);
            p.row.oosTrades = out.baseline.trades;
            p.row.oosExpectancyR = out.baseline.expectancyR;
            p.row.oosBeatsExpectancy = out.expectancyRank;
            std::vector<double> random;
            for (const ArmResult& a : out.random)
                random.push_back(a.expectancyR);
            p.row.oosScore = standardScore(out.baseline.expectancyR, random);
            p.luck = nullScores(random);
        }
        catch (const std::exception& e)
        {
            p.row.error = e.what();
        }
        return p;
    }

    bool better(const GridRow& a, const GridRow& b)
    {
        const bool aBad = !a.error.empty() || std::isnan(a.oosAdjustedBeats);
        const bool bBad = !b.error.empty() || std::isnan(b.oosAdjustedBeats);
        if (aBad || bBad)
            return !aBad && bBad;
        if (a.oosAdjustedBeats != b.oosAdjustedBeats)
            return a.oosAdjustedBeats > b.oosAdjustedBeats;
        return a.oosScore > b.oosScore;
    }

    std::string verdict(const GridRow& r, bool holdoutRun)
    {
        if (r.candidate)
            return "CANDIDATE";
        if (r.passedStageOne)
            return holdoutRun ? "failed holdout" : "finalist (no holdout given)";
        return "-";
    }

    std::string tableLine(const GridRow& r)
    {
        return fmt("  %-26s %-16s %-24s %8s %7d %9s %7s %6s %9s | %9s %7s %6s | %s\n", r.baseline.c_str(), r.c1.c_str(),
                   r.params.c_str(), num(r.inSampleExpectancyR, "%.3f").c_str(), r.oosTrades,
                   num(r.oosExpectancyR, "%.3f").c_str(), pct(r.oosBeatsExpectancy).c_str(), num(r.oosScore).c_str(),
                   pct(r.oosAdjustedBeats).c_str(),
                   std::isnan(r.holdoutExpectancyR) ? "-" : num(r.holdoutExpectancyR, "%.3f").c_str(),
                   std::isnan(r.holdoutBeatsExpectancy) ? "-" : pct(r.holdoutBeatsExpectancy).c_str(),
                   std::isnan(r.holdoutBar) ? "-" : pct(r.holdoutBar).c_str(),
                   verdict(r, !std::isnan(r.holdoutExpectancyR)).c_str());
    }
}

double standardScore(double value, const std::vector<double>& samples)
{
    if (std::isnan(value))
        return std::nan("");
    double sum = 0;
    int n = 0;
    for (double x : samples)
        if (!std::isnan(x))
        {
            sum += x;
            ++n;
        }
    if (n < 2)
        return std::nan("");
    const double mean = sum / n;
    double squares = 0;
    for (double x : samples)
        if (!std::isnan(x))
            squares += (x - mean) * (x - mean);
    const double sd = std::sqrt(squares / (n - 1));
    return sd > 0 ? (value - mean) / sd : std::nan("");
}

std::vector<double> nullScores(const std::vector<double>& samples)
{
    std::vector<double> out;
    for (double x : samples)
    {
        const double z = standardScore(x, samples);
        if (std::isnan(z))
            return {};
        out.push_back(z);
    }
    return out;
}

double familyAdjustedBeats(double score, const std::vector<double>& luck, int familySize)
{
    if (familySize < 1)
        throw std::invalid_argument("the number of pairs tried must be at least 1");
    if (std::isnan(score) || luck.empty())
        return std::nan("");
    std::size_t atOrAbove = 0;
    for (double z : luck)
        if (z >= score)
            ++atOrAbove;
    const double share = static_cast<double>(luck.size() - atOrAbove) / static_cast<double>(luck.size() + 1);
    return std::pow(share, familySize);
}

std::vector<GridRow> runGrid(const std::vector<GridBaseline>& baselines, const std::vector<GridC1>& c1s,
                             const AlignedData& inSample, const AlignedData& outOfSample, const GridOptions& options,
                             const AlignedData* holdout)
{
    if (baselines.empty() || c1s.empty())
        throw std::invalid_argument("the grid needs at least one baseline and one C1");
    if (options.controlRuns < 1)
        throw std::invalid_argument("the grid needs at least one random-direction run");
    if (options.priorTries < 0)
        throw std::invalid_argument("the number of earlier tries cannot be negative");
    if (options.nnfx.exitOnCross)
        throw std::invalid_argument("the grid cannot use the cross-back exit (the control needs it off)");
    if (options.nnfx.components.confirmation)
        throw std::invalid_argument("the grid puts its own C1 in: leave the confirmation unset");

    std::vector<Pair> pairs;
    for (std::size_t b = 0; b < baselines.size(); ++b)
        for (std::size_t c = 0; c < c1s.size(); ++c)
        {
            Pair p = runPair(baselines[b], c1s[c], inSample, outOfSample, options);
            p.baseline = b;
            p.c1 = c;
            pairs.push_back(std::move(p));
        }

    // the correction: every random run of every pair is one draw of luck; the best of familySize pairs is judged against it
    std::vector<double> luck;
    for (const Pair& p : pairs)
        luck.insert(luck.end(), p.luck.begin(), p.luck.end());
    const int familySize = static_cast<int>(pairs.size()) + options.priorTries;

    int finalists = 0;
    for (Pair& p : pairs)
    {
        GridRow& r = p.row;
        r.familySize = familySize;
        if (!r.error.empty())
            continue;
        r.oosAdjustedBeats = familyAdjustedBeats(r.oosScore, luck, familySize);
        r.passedStageOne = r.oosBeatsExpectancy >= options.minBeats && r.oosExpectancyR >= options.minExpectancy &&
                           r.oosAdjustedBeats >= options.minBeats && r.inSampleExpectancyR > 0.0;
        if (r.passedStageOne)
            ++finalists;
    }

    // the holdout: once per finalist, with the pair's own parameters; the bar allows for the number of finalists
    const double bar = finalists > 2 ? std::pow(options.minBeats, 1.0 / finalists) : options.minBeats;
    if (holdout)
        for (Pair& p : pairs)
        {
            GridRow& r = p.row;
            if (!r.passedStageOne)
                continue;
            r.holdoutBar = bar;
            try
            {
                const ControlResult h = runDirectionControl(*holdout, baselines[p.baseline].params, baselines[p.baseline].info.nnfx,
                                                            p.nnfx, options.controlRuns, options.run);
                r.holdoutExpectancyR = h.baseline.expectancyR;
                r.holdoutTrades = h.baseline.trades;
                r.holdoutBeatsExpectancy = h.expectancyRank;
                r.candidate = r.holdoutBeatsExpectancy >= bar && r.holdoutExpectancyR >= options.minExpectancy;
            }
            catch (const std::exception& e)
            {
                r.error = e.what();
                r.candidate = false;
            }
        }

    std::vector<GridRow> rows;
    for (Pair& p : pairs)
        rows.push_back(std::move(p.row));
    std::stable_sort(rows.begin(), rows.end(), better);
    return rows;
}

std::string gridTable(const std::vector<GridRow>& rows, const GridOptions& options, std::size_t top)
{
    int finalists = 0, candidates = 0, family = static_cast<int>(rows.size()) + options.priorTries;
    bool holdoutRun = false;
    for (const GridRow& r : rows)
    {
        finalists += r.passedStageOne ? 1 : 0;
        candidates += r.candidate ? 1 : 0;
        holdoutRun = holdoutRun || !std::isnan(r.holdoutExpectancyR);
        if (r.familySize > 0)
            family = r.familySize;
    }

    std::string out = fmt("GRID: %zu pairs tried (+ %d earlier), NNFX trading without the cross-back exit, %d random-direction runs each\n",
                          rows.size(), options.priorTries, options.controlRuns);
    out += fmt("finalist  = out-of-sample expectancy >= %sR, beats >= %s of its own random runs, still beats >= %s after allowing for\n"
               "            the %d pairs tried (adj.beats), and in-sample expectancy > 0\n"
               "candidate = a finalist that also passes the same expectancy and beats bars once on the holdout (the beats bar is\n"
               "            raised to minBeats^(1/F) when there are more than 2 finalists)\n\n",
               num(options.minExpectancy).c_str(), pct(options.minBeats).c_str(), pct(options.minBeats).c_str(), family);

    out += fmt("  %-26s %-16s %-24s %8s %7s %9s %7s %6s %9s | %9s %7s %6s | %s\n", "baseline", "C1", "parameters", "IS exp R",
               "trades", "OOS exp R", "beats", "z", "adj.beats", "hold exp", "beats", "bar", "verdict");
    std::size_t listed = 0;
    for (const GridRow& r : rows)
    {
        if (!r.error.empty())
            continue;
        if (listed < top || r.passedStageOne)
        {
            out += tableLine(r);
            ++listed;
        }
    }
    for (const GridRow& r : rows)
        if (!r.error.empty())
            out += fmt("  %-26s %-16s ERROR: %s\n", r.baseline.c_str(), r.c1.c_str(), r.error.c_str());

    out += fmt("\n%d finalist%s, %d candidate%s%s\n", finalists, finalists == 1 ? "" : "s", candidates, candidates == 1 ? "" : "s",
               holdoutRun || finalists == 0 ? "" : " (no holdout was given, so a finalist is not yet confirmed)");
    out += fmt("With %d pairs tried, the chance that at least one beats %s of its own random runs by luck alone is %s even if\n"
               "nothing works: the plain 'beats' column proves little; adj.beats already allows for it.\n",
               family, pct(options.minBeats).c_str(), pct(1.0 - std::pow(options.minBeats, std::max(family, 1))).c_str());
    out += "The full table (every pair) is in the grid log.\n";
    return out;
}

std::string gridCsvHeader()
{
    return "baseline,params,c1,in_sample_trades,in_sample_expectancy_r,oos_trades,oos_expectancy_r,oos_beats_expectancy,"
           "oos_score,oos_adjusted_beats,family_size,passed_stage_one,holdout_trades,holdout_expectancy_r,"
           "holdout_beats_expectancy,holdout_bar,candidate,error\n";
}

std::string gridCsvRow(const GridRow& r)
{
    std::string out = csvField(r.baseline) + "," + csvField(r.params) + "," + csvField(r.c1);
    out += "," + std::to_string(r.inSampleTrades) + "," + num(r.inSampleExpectancyR, "%.6g");
    out += "," + std::to_string(r.oosTrades) + "," + num(r.oosExpectancyR, "%.6g");
    for (double v : {r.oosBeatsExpectancy, r.oosScore, r.oosAdjustedBeats})
        out += "," + num(v, "%.6g");
    out += "," + std::to_string(r.familySize) + "," + (r.passedStageOne ? "yes" : "no");
    out += "," + std::to_string(r.holdoutTrades) + "," + num(r.holdoutExpectancyR, "%.6g") + "," +
           num(r.holdoutBeatsExpectancy, "%.6g") + "," + num(r.holdoutBar, "%.6g");
    return out + "," + (r.candidate ? "yes" : "no") + "," + csvField(r.error) + "\n";
}

std::string gridCsv(const std::vector<GridRow>& rows)
{
    std::string out = gridCsvHeader();
    for (const GridRow& r : rows)
        out += gridCsvRow(r);
    return out;
}

// ---------- the plan file ----------

namespace
{
    std::string trimmed(const std::string& s)
    {
        const std::size_t a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos)
            return "";
        return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
    }

    [[noreturn]] void bad(int line, const std::string& why)
    {
        throw std::invalid_argument("grid plan, line " + std::to_string(line) + ": " + why);
    }
}

GridPlan parseGridPlan(const std::string& text)
{
    GridPlan plan;
    std::istringstream in(text);
    std::string raw;
    int line = 0;
    while (std::getline(in, raw))
    {
        ++line;
        const std::size_t hash = raw.find('#');
        const std::string content = trimmed(hash == std::string::npos ? raw : raw.substr(0, hash));
        if (content.empty())
            continue;

        const std::size_t space = content.find_first_of(" \t");
        const std::string keyword = content.substr(0, space);
        const std::string rest = space == std::string::npos ? "" : trimmed(content.substr(space));
        if (keyword != "baseline" && keyword != "c1")
            bad(line, "expected 'baseline <name> <parameters>' or 'c1 <name>', got '" + content + "'");
        if (rest.empty())
            bad(line, "'" + keyword + "' needs a name");

        const std::size_t end = rest.find_first_of(" \t");
        const std::string name = rest.substr(0, end);
        const std::string after = end == std::string::npos ? "" : trimmed(rest.substr(end));
        if (keyword == "c1")
        {
            if (!after.empty())
                bad(line, "'c1' takes just a name (got '" + after + "' after it)");
            if (std::find(plan.c1s.begin(), plan.c1s.end(), name) != plan.c1s.end())
                bad(line, "c1 '" + name + "' is listed twice");
            plan.c1s.push_back(name);
        }
        else
        {
            for (const auto& b : plan.baselines)
                if (b.first == name)
                    bad(line, "baseline '" + name + "' is listed twice");
            plan.baselines.emplace_back(name, after);
        }
    }
    if (plan.baselines.empty())
        throw std::invalid_argument("grid plan: no 'baseline' lines");
    if (plan.c1s.empty())
        throw std::invalid_argument("grid plan: no 'c1' lines");
    return plan;
}

GridPlan loadGridPlan(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot read " + path);
    std::ostringstream text;
    text << file.rdbuf();
    return parseGridPlan(text.str());
}

ResolvedGrid resolveGridPlan(const GridPlan& plan)
{
    ResolvedGrid out;
    for (const auto& [name, paramText] : plan.baselines)
    {
        const BaselineInfo* info = findBaseline(name);
        if (!info)
            throw std::invalid_argument("unknown baseline '" + name + "' (available: " + baselineNames() + ")");
        try
        {
            out.baselines.push_back({*info, parseParams(paramText, info->ranges)});
        }
        catch (const std::invalid_argument& e)
        {
            throw std::invalid_argument("baseline " + name + ": " + e.what());
        }
    }
    for (const std::string& name : plan.c1s)
    {
        const ComponentInfo* c = findComponent(name);
        if (!c)
            throw std::invalid_argument("unknown C1 '" + name + "' (available: " + componentNames() + ")");
        if (c->kind != ComponentInfo::Kind::State)
            throw std::invalid_argument("'" + name + "' is a gate component: a C1 must be a state component");
        out.c1s.push_back({name, c->makeState(c->defaults)});
    }
    return out;
}
