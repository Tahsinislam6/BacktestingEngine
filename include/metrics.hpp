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
    double calmar{};           

    // from the closed trades (open trades are ignored) 
    int    trades{};
    double winRate{};          
    double profitFactor{};     
    double expectancy{};       
    double avgHoldingTime{};   // in the data's timestamp units (milliseconds); see kTimestampUnitsPerDay
};

Metrics computeMetrics(const BacktestResult& result, double startingCash, double periodsPerYear = 260.0);
