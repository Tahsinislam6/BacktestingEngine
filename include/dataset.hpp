#pragma once

#include <string>
#include <vector>

#include "aligned.hpp"

// "EURUSD_1d_2016-01-01_2025-12-31.csv" -> "EURUSD" and "EUR_GBP_forex_1d_2025.csv" -> "EUR_GBP".
// The folder part and the .csv are dropped, and so is everything from "_forex" or "_1d" on.
// A name with neither keeps its whole stem.
std::string symbolFromFilename(const std::string& path);

// The .csv files directly inside a folder (any capitalisation of the extension), sorted by name.
// Throws std::runtime_error if the folder does not exist.
std::vector<std::string> csvFilesIn(const std::string& folder);

// Loads every .csv in the folder and aligns them into one dataset, one asset per file (in name order).
// By default each file's Sunday bars are first folded into the following Monday (see sundays.hpp);
// pass mergeSundays = false to keep every bar as it is in the file.
// Throws std::runtime_error if there are no .csv files or two files give the same symbol.
AlignedData loadFolder(const std::string& folder, bool mergeSundays = true);
