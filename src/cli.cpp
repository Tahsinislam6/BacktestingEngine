#include "cli.hpp"

#include <cmath>
#include <stdexcept>

namespace
{
    double toDouble(const std::string& flag, const std::string& text)
    {
        std::size_t used = 0;
        double v = 0;
        try { v = std::stod(text, &used); }
        catch (const std::exception&) { used = 0; }
        if (used == 0 || used != text.size() || !std::isfinite(v))
            throw std::invalid_argument(flag + " needs a number, got '" + text + "'");
        return v;
    }

    long long toWholeNumber(const std::string& flag, const std::string& text, long long minimum)
    {
        std::size_t used = 0;
        long long v = 0;
        try { v = std::stoll(text, &used); }
        catch (const std::exception&) { used = 0; }
        if (used == 0 || used != text.size())
            throw std::invalid_argument(flag + " needs a whole number, got '" + text + "'");
        if (v < minimum)
            throw std::invalid_argument(flag + " must be at least " + std::to_string(minimum));
        return v;
    }
}

CliOptions parseArgs(const std::vector<std::string>& args)
{
    CliOptions o;
    bool multiplierGiven = false, baselineGiven = false, paramsGiven = false, gridOptionGiven = false, markupGiven = false;

    for (std::size_t i = 0; i < args.size(); ++i)
    {
        const std::string& flag = args[i];
        if (flag == "--help" || flag == "-h")
        {
            o.help = true;
            continue;
        }

        if (flag == "--keep-sundays")
        {
            o.keepSundays = true;
            continue;
        }

        if (flag == "--one-candle")
        {
            o.oneCandle = true;
            continue;
        }

        if (flag == "--continuation")
        {
            o.continuation = true;
            continue;
        }

        if (flag == "--no-cross-exit")
        {
            o.noCrossExit = true;
            continue;
        }

        if (flag == "--no-costs")
        {
            o.noCosts = true;
            continue;
        }

        // the word after the flag; it can't be another flag
        auto value = [&]() -> const std::string&
        {
            if (i + 1 >= args.size() || args[i + 1].rfind("--", 0) == 0)
                throw std::invalid_argument("missing value after " + flag);
            return args[++i];
        };

        if (flag == "--baseline")            { o.baseline = value(); baselineGiven = true; }
        else if (flag == "--params")         { o.params = value(); paramsGiven = true; }
        else if (flag == "--grid")           o.gridFile = value();
        else if (flag == "--holdout")        { o.holdoutDir = value(); gridOptionGiven = true; }
        else if (flag == "--ledger")         { o.ledgerFile = value(); gridOptionGiven = true; }
        else if (flag == "--prior-tries")    { o.priorTries = static_cast<int>(toWholeNumber(flag, value(), 0)); gridOptionGiven = true; }
        else if (flag == "--min-expectancy") { o.minExpectancy = toDouble(flag, value()); gridOptionGiven = true; }
        else if (flag == "--min-beats")
        {
            o.minBeats = toDouble(flag, value());
            if (!(o.minBeats >= 0.0 && o.minBeats <= 1.0))
                throw std::invalid_argument("--min-beats must be between 0 and 1");
            gridOptionGiven = true;
        }
        else if (flag == "--insample")       o.inSampleDir = value();
        else if (flag == "--outsample")      o.outSampleDir = value();
        else if (flag == "--threads")        o.threads = static_cast<int>(toWholeNumber(flag, value(), 0));
        else if (flag == "--entry-control")  { o.entryControl = static_cast<int>(toWholeNumber(flag, value(), 0)); }
        else if (flag == "--signal")
        {
            const std::string& text = value();
            const std::size_t colon = text.find(':');
            if (colon == std::string::npos)
                throw std::invalid_argument("--signal needs kind:lookback, e.g. breakout:20 or momentum:60, got '" + text + "'");
            const std::string kind = text.substr(0, colon);
            if (kind != "breakout" && kind != "momentum")
                throw std::invalid_argument("--signal kind must be breakout or momentum, got '" + kind + "'");
            o.signalKind = kind;
            o.signalLookback = static_cast<int>(toWholeNumber(flag, text.substr(colon + 1), 1));
        }
        else if (flag == "--entry-study")    { o.entryStudy = static_cast<int>(toWholeNumber(flag, value(), 0)); }
        else if (flag == "--component-control") { o.componentControl = static_cast<int>(toWholeNumber(flag, value(), 0)); }
        else if (flag == "--confirm")        { o.confirm = value(); }
        else if (flag == "--exit-indicator") { o.exitIndicator = value(); }
        else if (flag == "--filter")         { o.filter = value(); }
        else if (flag == "--confirm-window") { o.confirmWindow = static_cast<int>(toWholeNumber(flag, value(), 1)); }
        else if (flag == "--max-per-currency") { o.maxPerCurrency = static_cast<int>(toWholeNumber(flag, value(), 1)); }
        else if (flag == "--max-open-risk")
        {
            const double v = toDouble(flag, value());
            if (!(v > 0.0))
                throw std::invalid_argument("--max-open-risk must be above 0 (e.g. 0.06 for 6% of equity)");
            o.maxOpenRisk = v;
        }
        else if (flag == "--log-dir")        o.logDir = value();
        else if (flag == "--note")           o.note = value();
        else if (flag == "--control")        o.control = static_cast<int>(toWholeNumber(flag, value(), 0));
        else if (flag == "--costs")          o.costsFile = value();
        else if (flag == "--rates")          o.ratesFile = value();
        else if (flag == "--swap-markup")
        {
            o.swapMarkup = toDouble(flag, value());
            if (o.swapMarkup < 0.0)
                throw std::invalid_argument("--swap-markup must not be negative");
            markupGiven = true;
        }
        else if (flag == "--spread-multiplier")
        {
            o.spreadMultiplier = toDouble(flag, value());
            if (o.spreadMultiplier < 0.0)
                throw std::invalid_argument("--spread-multiplier must not be negative");
            multiplierGiven = true;
        }
        else if (flag == "--cash")
        {
            o.startingCash = toDouble(flag, value());
            if (!(o.startingCash > 0.0))
                throw std::invalid_argument("--cash must be positive");
        }
        else
            throw std::invalid_argument("unknown option: " + flag);
    }

    if (o.help)
        return o;
    if (o.inSampleDir.empty())
        throw std::invalid_argument("--insample is required");

    if (gridOptionGiven && o.gridFile.empty())
        throw std::invalid_argument("--holdout, --prior-tries, --min-beats, --min-expectancy and --ledger only apply with --grid");

    if (!o.gridFile.empty())
    {
        // the grid: every baseline x C1 pair of the plan file, in one batch. Every pair tried must be on record.
        if (o.outSampleDir.empty())
            throw std::invalid_argument("--grid needs --outsample (the second folder of the selection data)");
        if (o.logDir.empty() || o.note.empty())
            throw std::invalid_argument("--grid needs --log-dir and --note: every pair tried is a try, so it is logged, "
                                        "with the pass rule written down before the run");
        if (baselineGiven || paramsGiven || !o.confirm.empty() || !o.exitIndicator.empty() || !o.filter.empty() ||
            !o.signalKind.empty() || o.entryStudy > 0 || o.entryControl > 0 || o.componentControl > 0)
            throw std::invalid_argument("--grid takes its baselines, parameters and C1s from the plan file: do not combine it "
                                        "with --baseline, --params, --confirm, --exit-indicator, --filter, --signal, "
                                        "--entry-study, --entry-control or --component-control");
    }
    else if (!o.signalKind.empty())
    {
        if (o.entryStudy == 0)
            throw std::invalid_argument("--signal needs --entry-study <runs> (the signal study is the entry study)");
        if (baselineGiven || paramsGiven || o.control > 0 || o.componentControl > 0 || o.entryControl > 0)
            throw std::invalid_argument("--signal has no baseline: do not combine it with --baseline, --params, "
                                        "--control, --component-control or --entry-control");
    }
    else
    {
        // a system: a baseline with fixed parameters and at least one component. Nothing is searched or tuned, and a
        // baseline is never tested alone.
        if (!baselineGiven)
            throw std::invalid_argument("--baseline is required (or --signal for a study without a baseline, or --grid)");
        if (!paramsGiven)
            throw std::invalid_argument("--params is required: the baseline's parameters are fixed in advance, e.g. "
                                        "--params period=26 (--params \"\" for a baseline without parameters)");
        if (o.confirm.empty() && o.exitIndicator.empty() && o.filter.empty())
            throw std::invalid_argument("a baseline is never tested alone: give at least one of --confirm, "
                                        "--exit-indicator or --filter");
    }
    if (!o.note.empty() && o.logDir.empty())
        throw std::invalid_argument("--note needs --log-dir (it is written into the log rows)");
    if (!o.logDir.empty() && o.gridFile.empty() && o.control == 0 && o.componentControl == 0 && o.entryStudy == 0 && o.entryControl == 0)
        throw std::invalid_argument("--log-dir needs --grid, --control, --component-control, --entry-study or --entry-control "
                                    "(only those results are logged)");
    if (o.entryControl > 0 && o.confirm.empty() && o.filter.empty())
        throw std::invalid_argument("--entry-control needs --confirm or --filter to control (an exit indicator does not pick entries)");
    if (o.control > 0 && !o.noCrossExit && o.gridFile.empty())
        throw std::invalid_argument("--control needs --no-cross-exit (a random trade may start against the line)");
    if (markupGiven && o.ratesFile.empty())
        throw std::invalid_argument("--swap-markup needs --rates (the markup is added to the swap from the rates file)");
    if (o.noCosts && (!o.costsFile.empty() || multiplierGiven))
        throw std::invalid_argument("--no-costs cannot be combined with --costs or --spread-multiplier");
    return o;
}

std::string usage()
{
    return
        "Usage: backtester --insample <folder> --baseline <name> --params <values> --confirm <name> [options]\n"
        "\n"
        "A run trades one NNFX system (or, with --grid, every baseline x C1 pair of a plan): the baseline's signal, the components you choose, ATR stops and targets. Nothing is\n"
        "searched or tuned, and a baseline is never run alone: fix the parameters and the components first.\n"
        "\n"
        "  --insample <folder>      folder of .csv files, run first (required)\n"
        "  --outsample <folder>     folder of .csv files the same system is run on as well\n"
        "  --baseline <name>        the entry signal: price crossing this line\n"
        "  --params <values>        the baseline's parameters, e.g. period=26 or period=15,slow=30 (every one, inside its range;\n"
        "                           \"\" for a baseline without parameters)\n"
        "  --confirm <name>         a state component (e.g. rsi) that must agree with the direction at entry\n"
        "  --confirm-window <n>     bars a signal waits for its confirmation (default 7)\n"
        "  --exit-indicator <name>  a state component that closes a trade when it turns against it\n"
        "  --filter <name>          a gate component (e.g. atr_expansion) that must allow the entry\n"
        "  --one-candle             a signal that cannot be taken on the cross bar gets one more bar\n"
        "  --continuation           re-enter while price stays on the same side of the baseline\n"
        "  --max-open-risk <x>      cap on the total risk of open signals as a share of equity (0.06 = 6%)\n"
        "  --max-per-currency <n>   cap on net signals per currency (long EURUSD and GBPUSD both short USD)\n"
        "  --grid <plan file>       test EVERY baseline x C1 pair listed in the plan file in one batch (no --baseline/--params/\n"
        "                           --confirm): each pair vs random directions, a multiple-testing correction for the number of\n"
        "                           pairs, finalists run once on --holdout; needs --outsample, --log-dir and --note (--control\n"
        "                           sets the random runs per pair, default 200)\n"
        "  --holdout <folder>       grid: data the finalists are run on ONCE with their fixed parameters (never chosen on)\n"
        "  --prior-tries <n>        grid: pairs already tried on this data, counted in the correction (default 0)\n"
        "  --min-beats <x>          grid: share of random runs a pair must beat, also after the correction (default 0.9)\n"
        "  --min-expectancy <x>     grid: expectancy in R a pair must reach (default 0.03)\n"
        "  --ledger <file>          grid: append one row (what, data, rule, result) to this tries ledger\n"
        "  --entry-study <n>        what price does after the system's signals (1-40 bars, target/stop race, excursions),\n"
        "                           in ATR, against n random-direction runs; before any stops, targets or sizing\n"
        "  --signal <kind:n>        entry study of a breakout (n-bar high/low) or momentum (n-bar return flips) signal instead of a\n"
        "                           baseline (no --baseline/--params); needs --entry-study\n"
        "  --entry-control <n>      does the chosen --confirm / --filter pick better entries than n random components that\n"
        "                           behave the same? Measured on the entry study (no stops or sizing)\n"
        "  --component-control <n>  compare each chosen component with n random ones that behave the same\n"
        "  --control <n>            compare the baseline's direction with n random-direction runs (needs --no-cross-exit)\n"
        "  --no-cross-exit          do not close when the baseline crosses back (stop, target, breakeven only)\n"
        "  --log-dir <folder>       append every control / entry-study result to the csv logs in this folder (one file per test type)\n"
        "  --note <text>            with --log-dir: why this test was run and the pass rule (kept in the log rows)\n"
        "  --threads <n>            worker threads (default: all cores)\n"
        "  --cash <amount>          starting cash (default 100000)\n"
        "  --costs <file>           spreads in pips, rows of symbol,spread_pips; listed pairs replace the typical values\n"
        "  --spread-multiplier <x>  scale every spread (e.g. 2 to stress-test; default 1)\n"
        "  --no-costs               trade with no spread at all (default: typical spreads)\n"
        "  --rates <file>           turn swap (financing) on: 'currency,date,rate' annual interest rates in percent; a long earns\n"
        "                           base rate - quote rate per night held, a short pays it (default: no swap)\n"
        "  --swap-markup <pct>      with --rates: the broker's markup in percentage points a year, charged on every position\n"
        "  --keep-sundays           keep the small Sunday bars as they are (default: fold them into Monday)\n"
        "  --help                   show this text\n";
}
