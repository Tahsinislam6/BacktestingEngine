#pragma once

#include <string>
#include "bar.hpp"

// Reads the first 5 columns as timestamp,open,high,low,close.
// The timestamp must be a whole number (milliseconds since 1970 in the current data files). It is stored
// exactly as written, with no conversion. Throws std::invalid_argument if it is anything else.
// `symbol` is stored in the returned series so several series can be told apart.
BarSeries loadCsv(const std::string& path, const std::string& symbol = "");