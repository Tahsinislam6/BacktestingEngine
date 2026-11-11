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
                std::optional<double> tp = std::nullopt);
        
        bool close(int id, double price, std::int64_t time, ExitReason why);

        double equity(double markPrice) const;

        const std::vector<Trade>& openTrades() const {return open_;}
        const std::vector<ClosedTrade>& closedTrades() const {return closed_;}

    private:
        double cash_{};
        int nextId_{1};
        std::vector<Trade> open_;
        std::vector<ClosedTrade> closed_;
};