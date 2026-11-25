#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "portfolio_job.hpp"

// The JSON side of the portfolio job.
//
// Request (POST /api/portfolios). Required: sleeves. Optional: commonPeriodOnly (false), periodsPerYear (260). Each sleeve:
// name, dataset, strategy, cash (required) and params (optional). Any other key is an error, so a typo is never ignored.
//   {"sleeves": [{"name": "BTC", "dataset": "btc.v.txt", "strategy": "inertia_breakout",
//                 "params": {"lookback": 35, "allowance": 5}, "cash": 3000}, ...],
//    "commonPeriodOnly": false, "periodsPerYear": 365}
//
// Result: {"kind": "portfolio", "spec": {...as run...}, "bars", "totalCash", "timestamps": [...],
//          "combined": {"cash", "finalEquity", "equity": [...], "metrics": {...}},
//          "sleeves": [{"name", "dataset", "strategy", "params", "cash", "finalEquity", "equity": [...], "metrics": {...}}],
//          "trades": [closed trades, each with "sleeve"], "openTrades": [... each with "sleeve"]}
// A number that is not finite is null.

// Throws std::invalid_argument if the JSON is not an object of the right shape (the values are checked by validatePortfolioSpec).
PortfolioSpec portfolioSpecFromJson(const nlohmann::json& request);

nlohmann::json toJson(const PortfolioSpec& spec);
nlohmann::json toJson(const PortfolioOutput& output);

// Request text in, result text out. Throws std::invalid_argument for text that is not valid JSON or not a valid request,
// and whatever runPortfolio throws.
std::string runPortfolioJson(const StrategyRegistry& registry, const std::string& dataDir, const std::string& requestText);
