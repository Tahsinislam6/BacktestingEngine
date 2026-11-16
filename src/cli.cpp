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

        // the word after the flag; it can't be another flag
        auto value = [&]() -> const std::string&
        {
            if (i + 1 >= args.size() || args[i + 1].rfind("--", 0) == 0)
                throw std::invalid_argument("missing value after " + flag);
            return args[++i];
        };

        if (flag == "--baseline")            o.baseline = value();
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
        "  --objective <name>       sharpe (default), sortino, calmar, return or capture\n"
        "  --min-trades <n>         ignore parameter sets with fewer closed trades (default 30)\n"
        "  --train-fraction <x>     share of the years used for training in the time split (default 0.7)\n"
        "  --walk-train <bars>      walk-forward training window in days; give with --walk-test\n"
        "  --walk-test <bars>       walk-forward test window in days\n"
        "  --csv <file>             write every ranked run to this file\n"
        "  --threads <n>            worker threads (default: all cores)\n"
        "  --cash <amount>          starting cash (default 100000)\n"
        "  --top <n>                rows to show in the ranked table (default 15)\n"
        "  --keep-sundays           keep the small Sunday bars as they are (default: fold them into Monday)\n"
        "  --help                   show this text\n";
}
