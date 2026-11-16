#pragma once

#include <string>
#include <vector>

#include "aligned.hpp"

// "EUR_GBP_forex_1d_2025-01-01_2025-12-31.csv" -> "EUR_GBP". The folder part and the .csv are dropped,
// and so is everything from "_forex" on. A name without "_forex" keeps its whole stem.
std::string symbolFromFilename(const std::string& path);

// The .csv files directly inside a folder (any capitalisation of the extension), sorted by name.
// Throws std::runtime_error if the folder does not exist.
std::vector<std::string> csvFilesIn(const std::string& folder);

// Loads every .csv in the folder and aligns them into one dataset, one asset per file (in name order).
// Throws std::runtime_error if there are no .csv files or two files give the same symbol.
AlignedData loadFolder(const std::string& folder);
