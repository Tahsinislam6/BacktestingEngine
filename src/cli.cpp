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

    Mode toMode(const std::string& text)
    {
        if (text == "flip") return Mode::Flip;
        if (text == "nnfx") return Mode::Nnfx;
        throw std::invalid_argument("--mode must be flip or nnfx, got '" + text + "'");
    }

    Objective toObjective(const std::string& text)
    {
        if (text == "sharpe")  return Objective::Sharpe;
        if (text == "sortino") return Objective::Sortino;
        if (text == "calmar")  return Objective::Calmar;
        if (text == "return")  return Objective::TotalReturn;
        if (text == "capture") return Objective::CaptureEfficiency;
        throw std::invalid_argument("--objective must be sharpe, sortino, calmar, return or capture, got '" + text + "'");
    }
}

CliOptions parseArgs(const std::vector<std::string>& args)
{
    CliOptions o;
    bool multiplierGiven = false, modeGiven = false, baselineGiven = false, nnfxOnly = false;

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
            nnfxOnly = true;
            continue;
        }

        if (flag == "--continuation")
        {
            o.continuation = true;
            nnfxOnly = true;
            continue;
        }

        if (flag == "--screen")
        {
            o.screen = true;
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
        else if (flag == "--mode")           { o.mode = toMode(value()); modeGiven = true; }
        else if (flag == "--insample")       o.inSampleDir = value();
        else if (flag == "--outsample")      o.outSampleDir = value();
        else if (flag == "--csv")            o.csvOut = value();
        else if (flag == "--objective")      o.objective = toObjective(value());
        else if (flag == "--min-trades")     o.minTrades = static_cast<int>(toWholeNumber(flag, value(), 0));
        else if (flag == "--threads")        o.threads = static_cast<int>(toWholeNumber(flag, value(), 0));
        else if (flag == "--walk-train")     o.walkTrain = static_cast<std::size_t>(toWholeNumber(flag, value(), 0));
        else if (flag == "--walk-test")      o.walkTest = static_cast<std::size_t>(toWholeNumber(flag, value(), 0));
        else if (flag == "--top")            o.top = static_cast<std::size_t>(toWholeNumber(flag, value(), 0));
        else if (flag == "--train-fraction")
        {
            o.trainFraction = toDouble(flag, value());
            if (!(o.trainFraction > 0.0 && o.trainFraction < 1.0))
                throw std::invalid_argument("--train-fraction must be between 0 and 1");
        }
        else if (flag == "--component-control") { o.componentControl = static_cast<int>(toWholeNumber(flag, value(), 0)); nnfxOnly = true; }
        else if (flag == "--confirm")        { o.confirm = value(); nnfxOnly = true; }
        else if (flag == "--exit-indicator") { o.exitIndicator = value(); nnfxOnly = true; }
        else if (flag == "--filter")         { o.filter = value(); nnfxOnly = true; }
        else if (flag == "--confirm-window") { o.confirmWindow = static_cast<int>(toWholeNumber(flag, value(), 1)); nnfxOnly = true; }
        else if (flag == "--max-per-currency") { o.maxPerCurrency = static_cast<int>(toWholeNumber(flag, value(), 1)); nnfxOnly = true; }
        else if (flag == "--max-open-risk")
        {
            const double v = toDouble(flag, value());
            if (!(v > 0.0))
                throw std::invalid_argument("--max-open-risk must be above 0 (e.g. 0.06 for 6% of equity)");
            o.maxOpenRisk = v;
            nnfxOnly = true;
        }
        else if (flag == "--screen-csv")     o.screenCsv = value();
        else if (flag == "--log-dir")        o.logDir = value();
        else if (flag == "--note")           o.note = value();
        else if (flag == "--min-beats")
        {
            o.minBeats = toDouble(flag, value());
            if (!(o.minBeats >= 0.0 && o.minBeats <= 1.0))
                throw std::invalid_argument("--min-beats must be between 0 and 1");
        }
        else if (flag == "--min-expectancy") o.minExpectancy = toDouble(flag, value());
        else if (flag == "--control")        o.control = static_cast<int>(toWholeNumber(flag, value(), 0));
        else if (flag == "--costs")          o.costsFile = value();
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
    if (!o.screenCsv.empty() && !o.screen)
        throw std::invalid_argument("--screen-csv needs --screen");
    if (o.screen)
    {
        if (o.outSampleDir.empty())
            throw std::invalid_argument("--screen needs --outsample (the verdict is based on data the winner was not tuned on)");
        if (baselineGiven || modeGiven)
            throw std::invalid_argument("--screen runs every baseline in nnfx mode; do not combine it with --baseline or --mode");
    }
    if (!o.screen && nnfxOnly && o.mode != Mode::Nnfx)
        throw std::invalid_argument("--confirm, --exit-indicator, --filter, --confirm-window, --one-candle, --continuation, "
                                    "--max-open-risk and --max-per-currency only apply with --mode nnfx (or --screen)");
    if (!o.note.empty() && o.logDir.empty())
        throw std::invalid_argument("--note needs --log-dir (it is written into the log rows)");
    if (!o.logDir.empty() && !o.screen && o.control == 0 && o.componentControl == 0)
        throw std::invalid_argument("--log-dir needs --screen, --control or --component-control (only those results are logged)");
    if (o.componentControl > 0 && o.screen)
        throw std::invalid_argument("--component-control cannot be combined with --screen");
    if (o.componentControl > 0 && o.confirm.empty() && o.exitIndicator.empty() && o.filter.empty())
        throw std::invalid_argument("--component-control needs --confirm, --exit-indicator or --filter to control");
    if (!o.screen && o.noCrossExit && o.mode != Mode::Nnfx)
        throw std::invalid_argument("--no-cross-exit only applies with --mode nnfx");
    if (!o.screen && o.control > 0 && o.mode != Mode::Nnfx)
        throw std::invalid_argument("--control only applies with --mode nnfx");
    if (!o.screen && o.control > 0 && !o.noCrossExit)
        throw std::invalid_argument("--control needs --no-cross-exit (a random trade may start against the line)");
    if (o.noCosts && (!o.costsFile.empty() || multiplierGiven))
        throw std::invalid_argument("--no-costs cannot be combined with --costs or --spread-multiplier");
    if ((o.walkTrain > 0) != (o.walkTest > 0))
        throw std::invalid_argument("--walk-train and --walk-test must be given together");
    return o;
}

std::string usage()
{
    return
        "Usage: backtester --insample <folder> [options]\n"
        "\n"
        "  --insample <folder>      folder of .csv files used to choose the parameters (required)\n"
        "  --outsample <folder>     folder of .csv files the winner is tested on once, never tuned on\n"
        "  --baseline <name>        which baseline to optimise (default: sma)\n"
        "  --mode <name>            flip (default: always in the market) or nnfx (NNFX entries, stops, targets)\n"
        "  --screen                 screen EVERY baseline: nnfx without the cross-back exit, winner tested out-of-sample\n"
        "                           against random directions; needs --outsample (--control sets the runs, default 200)\n"
        "  --screen-csv <file>      write the screen table as csv\n"
        "  --min-beats <x>          screen: share of random runs a candidate must beat (default 0.9)\n"
        "  --min-expectancy <x>     screen: expectancy in R a candidate must reach (default 0.03)\n"
        "  --confirm <name>         nnfx: a state component (e.g. rsi) that must agree with the direction at entry\n"
        "  --confirm-window <n>     nnfx: bars a signal waits for its confirmation (default 7)\n"
        "  --exit-indicator <name>  nnfx: a state component that closes a trade when it turns against it\n"
        "  --filter <name>          nnfx: a gate component (e.g. atr_expansion) that must allow the entry\n"
        "  --one-candle             nnfx: a signal that cannot be taken on the cross bar gets one more bar\n"
        "  --continuation           nnfx: re-enter while price stays on the same side of the baseline\n"
        "  --max-open-risk <x>      nnfx: cap on the total risk of open signals as a share of equity (0.06 = 6%)\n"
        "  --max-per-currency <n>   nnfx: cap on net signals per currency (long EURUSD and GBPUSD both short USD)\n"
        "  --component-control <n>  nnfx: compare each chosen component with n random ones that behave the same\n"
        "  --log-dir <folder>       append every screen / control result to the csv logs in this folder (one file per test type)\n"
        "  --note <text>            with --log-dir: why this test was run (kept in the log rows)\n"
        "  --no-cross-exit          nnfx: do not close when the baseline crosses back (stop, target, breakeven only)\n"
        "  --control <n>            nnfx: compare the winner's direction with n random-direction runs (needs --no-cross-exit)\n"
        "  --objective <name>       sharpe (default), sortino, calmar, return or capture\n"
        "  --min-trades <n>         ignore parameter sets with fewer closed trades (default 30)\n"
        "  --train-fraction <x>     share of the years used for training in the time split (default 0.7)\n"
        "  --walk-train <bars>      walk-forward training window in days; give with --walk-test\n"
        "  --walk-test <bars>       walk-forward test window in days\n"
        "  --csv <file>             write every ranked run to this file\n"
        "  --threads <n>            worker threads (default: all cores)\n"
        "  --cash <amount>          starting cash (default 100000)\n"
        "  --top <n>                rows to show in the ranked table (default 15)\n"
        "  --costs <file>           spreads in pips, rows of symbol,spread_pips; listed pairs replace the typical values\n"
        "  --spread-multiplier <x>  scale every spread (e.g. 2 to stress-test; default 1)\n"
        "  --no-costs               trade with no spread at all (default: typical spreads)\n"
        "  --keep-sundays           keep the small Sunday bars as they are (default: fold them into Monday)\n"
        "  --help                   show this text\n";
}
