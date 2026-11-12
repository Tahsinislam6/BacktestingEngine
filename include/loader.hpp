#pragma once

#include <string>
#include "bar.hpp"

// Reads the first 5 columns as timestamp,open,high,low,close.
// The timestamp may be epoch seconds or a date/datetime (see timestamp.hpp).
// `symbol` is stored in the returned series so several series can be told apart.
BarSeries loadCsv(const std::string& path, const std::string& symbol = "");