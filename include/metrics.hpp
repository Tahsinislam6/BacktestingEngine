#pragma once

#include "backtester.hpp"

struct Metrics
{
    // from the equity curve 
    double totalReturn{};      
    double cagr{};             
    double maxDrawdown{};      
    double sharpe{};           
    double sortino{};          
    double mar{};             

    // from the closed trades (open trades are ignored) 
    int    trades{};
    double winRate{};          
    double profitFactor{};     
    double expectancy{};       // the average pnl per trade, in account money
    double expectancyR{};      // the average of pnl / initial risk, where risk = |entry - initialStop| x quantity (a trade that
                               // lost exactly its stop is -1R); trades with no initial stop or no risk are left out
    int    tradesWithRisk{};   // how many trades expectancyR is the average of
    double avgHoldingTime{};   // in the data's timestamp units (milliseconds); see kTimestampUnitsPerDay
};

Metrics computeMetrics(const BacktestResult& result, double startingCash, double periodsPerYear = 260.0);
