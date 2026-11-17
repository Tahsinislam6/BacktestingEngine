#include "rstats.hpp"

#include <cmath>
#include <limits>

namespace
{
    // Adds the trades' R values one by one, then finishes the averages.
    class Accumulator
    {
    public:
        void add(double r, ExitReason reason)
        {
            ++stats_.trades;
            stats_.totalR += r;
            if (r > 0)
            {
                ++stats_.wins;
                won_ += r;
            }
            else if (r < 0)
            {
                ++losses_;
                lost_ += r;
            }

            ExitStats* by = &stats_.signal;
            switch (reason)
            {
                case ExitReason::StopLoss: by = &stats_.stop; break;
                case ExitReason::TakeProfit: by = &stats_.target; break;
                case ExitReason::Breakeven: by = &stats_.breakeven; break;
                case ExitReason::Trailing: by = &stats_.trailing; break;
                case ExitReason::Indicator: by = &stats_.indicator; break;
                case ExitReason::Signal: break;
            }
            ++by->count;
            by->totalR += r;
        }

        RStats finish() const
        {
            const double nan = std::numeric_limits<double>::quiet_NaN();
            RStats s = stats_;
            s.winRate = s.trades > 0 ? static_cast<double>(s.wins) / s.trades : nan;
            s.avgWinR = s.wins > 0 ? won_ / s.wins : nan;
            s.avgLossR = losses_ > 0 ? lost_ / losses_ : nan;
            s.expectancyR = s.trades > 0 ? s.totalR / s.trades : nan;
            if (lost_ < 0)
                s.profitFactor = won_ / -lost_;
            else
                s.profitFactor = won_ > 0 ? std::numeric_limits<double>::infinity() : nan;
            return s;
        }

    private:
        RStats stats_;
        double won_{0};
        double lost_{0};
        int losses_{0};
    };
}

RReport computeRReport(const std::vector<ClosedTrade>& closed, int openAtEnd)
{
    Accumulator all, targetHalf, runner;
    int withoutStop = 0;

    for (const ClosedTrade& c : closed)
    {
        // the risk the trade started with: a moved stop (breakeven, trailing) does not change what 1R was
        const double risk = c.trade.initialStop ? c.trade.quantity * std::abs(c.trade.entryPrice - *c.trade.initialStop) : 0.0;
        if (!(risk > 0.0))
        {
            ++withoutStop;
            continue;
        }

        const double r = c.pnl / risk;
        all.add(r, c.reason);
        (c.trade.takeProfit ? targetHalf : runner).add(r, c.reason);
    }

    RReport report;
    report.all = all.finish();
    report.targetHalf = targetHalf.finish();
    report.runner = runner.finish();
    report.withoutStop = withoutStop;
    report.openAtEnd = openAtEnd;
    return report;
}
