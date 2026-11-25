#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "sweep_job.hpp"

// The JSON side of the density sweep job.
//
// Request (POST /api/sweeps). Required: strategy, dataset, axes, metric, threshold, radius, minTrades, nullRuns, block, note
// (nothing about the test is picked for the user). Optional: params (fixed values of the parameters that are not swept),
// cash (10000), periodsPerYear (260). Any other key is an error, so a typo is never ignored.
//   {"strategy": "...", "dataset": "file.txt", "params": {"name": number},
//    "axes": [{"param": "lookback", "from": 5, "to": 200, "step": 5}, {"param": "allowance", "from": 1, "to": 60, "step": 1}],
//    "metric": "mar", "threshold": 0, "radius": 1, "minTrades": 30, "nullRuns": 200, "block": 5,
//    "cash": 10000, "periodsPerYear": 260, "note": "PASS if ..."}
//
// Result: {"kind": "sweep", "spec": {...as run...}, "bars", "cells", "ideasTried",
//          "axes": [{"param", "values": [...]}, {...}],
//          "grid": {"trades": [...], "values": [...], "valid": [...]},          one entry per cell, row-major
//          "real": {"validCells", "passDensity", "plateauDensity", "peakOverNeighbours"},
//          "shuffled": {"runs", "block", "passDensity": {"median", "p95", "atLeastReal", "values": [...]},
//                       "plateauDensity": {...same...}}}
// A number that is not finite is null. There is no verdict: the user judges against their own note.

// Throws std::invalid_argument if the JSON is not an object of the right shape (the values are checked by validateSweepSpec).
SweepSpec sweepSpecFromJson(const nlohmann::json& request);

nlohmann::json toJson(const SweepSpec& spec);
nlohmann::json toJson(const SweepOutput& output);

// Request text in, result text out. Throws std::invalid_argument for text that is not valid JSON or not a valid request,
// and whatever runSweep throws.
std::string runSweepJson(const StrategyRegistry& registry, const std::string& dataDir, const std::string& requestText);
