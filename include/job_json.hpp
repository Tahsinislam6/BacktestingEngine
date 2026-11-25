#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "job.hpp"

// The JSON side of the job runner: this is what the HTTP server and the web app exchange.
//
// Request:  {"strategy": "...", "dataset": "file.txt", "params": {"name": number}, "cash": 10000, "periodsPerYear": 260}
//           (strategy and dataset are required; every other key is an error, so a typo is never ignored)
// Result:   {"spec": {...as run, with every parameter...}, "metrics": {...}, "timestamps": [...], "equity": [...],
//            "trades": [closed trades], "openTrades": [...]}
// A number that is not finite (a profit factor with no losing trades) is written as null.

// Throws std::invalid_argument if the JSON is not an object of the right shape.
RunSpec runSpecFromJson(const nlohmann::json& request);

nlohmann::json toJson(const RunSpec& spec);
nlohmann::json toJson(const Metrics& metrics);
nlohmann::json toJson(const RunOutput& output);

// The strategies and their parameter schemas, for building forms: [{name, description, params: [{name, default, min,
// max, integer, description}]}].
nlohmann::json strategiesToJson(const StrategyRegistry& registry);

// Request text in, result text out. Throws std::invalid_argument for text that is not valid JSON or not a valid request,
// and whatever runJob throws.
std::string runJobJson(const StrategyRegistry& registry, const std::string& dataDir, const std::string& requestText);
