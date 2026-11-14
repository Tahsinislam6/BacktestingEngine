#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "trade.hpp"

class Portfolio
{
    public:
        explicit Portfolio(double startingCash);

        int open(Direction d, double qty, double price, std::int64_t time,
                std::optional<double> sl = std::nullopt,
                std::optional<double> tp = std::nullopt,
                int asset = 0);
        
        bool close(int id, double price, std::int64_t time, ExitReason why);

        double equity(double markPrice) const;

        // marks[asset] = price used to value open trades on that asset.
        // A trade whose asset has no mark is valued at its entry price (adds no PnL).
        double equity(const std::vector<double>& marks) const;

        // The engine stores the latest marks here every snapshot, so a strategy can just call equity().
        void setMarks(const std::vector<double>& marks) {marks_ = marks;}

        // Cash + open PnL at the marks stored by setMarks(). With no marks set, open trades add nothing.
        double equity() const {return equity(marks_);}

        const std::vector<Trade>& openTrades() const {return open_;}
        const std::vector<ClosedTrade>& closedTrades() const {return closed_;}

    private:
        double cash_{};
        int nextId_{1};
        std::vector<Trade> open_;
        std::vector<ClosedTrade> closed_;
        std::vector<double> marks_;
};