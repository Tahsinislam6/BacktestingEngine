#pragma once

#include <vector>

#include "bar.hpp"
#include "order.hpp"
#include "portfolio.hpp"

class Strategy
{
public:
    virtual ~Strategy() = default;

    virtual std::vector<Order> onBar(const Bar& bar, const Portfolio& portfolio) = 0;
};