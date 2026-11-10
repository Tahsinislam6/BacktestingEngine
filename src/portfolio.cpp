#include <algorithm>

#include "portfolio.hpp"

Portfolio::Portfolio(double startingCash) : cash_(startingCash) {}

int Portfolio::open(Direction d, double qty, double price, std::int64_t time,
        std::optional<double> sl, std::optional<double> tp)
{
    Trade t;
    t.id = nextId_++;
    t.direction = d;
    t.quantity = qty;
    t.entryPrice = price;
    t.entryTime = time;
    t.stopLoss = sl;
    t.takeProfit = tp;

    open_.push_back(t);
    return t.id;
}

bool Portfolio::close(int id, double price, std::int64_t time, ExitReason why)
{
    auto it = std::find_if(open_.begin(), open_.end(),
    [id](const Trade& t) {return t.id == id;});
    if (it == open_.end())
        return false;

    ClosedTrade c;
    c.trade = *it;
    c.exitPrice = price;
    c.exitTime = time;
    c.reason = why;
    c.pnl = pnlOf(*it, price);

    cash_ += c.pnl;
    closed_.push_back(c);
    open_.erase(it);

    return true;
}

double Portfolio::equity(double markPrice) const
{
    double total = cash_;
    for (const Trade t : open_)
        total += pnlOf(t, markPrice);
    return total;
}