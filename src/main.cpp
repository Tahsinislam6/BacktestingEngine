#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "aligned.hpp"
#include "loader.hpp"

// The bare-bones program: load every .csv in a folder, line the assets up on common timestamps and say what was found.
// Strategies are written against the Strategy / MultiStrategy interfaces and run with runBacktest / runMultiBacktest;
// metrics come from computeMetrics (see metrics.hpp).
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: backtester <folder with .csv files>\n";
        return 2;
    }

    try
    {
        std::vector<BarSeries> series;
        for (const auto& entry : std::filesystem::directory_iterator(argv[1]))
        {
            if (entry.path().extension() == ".csv")
                series.push_back(loadCsv(entry.path().string(), entry.path().stem().string()));
        }
        if (series.empty())
        {
            std::cerr << "no .csv files in " << argv[1] << "\n";
            return 1;
        }

        const AlignedData data = align(series);
        std::cout << data.symbols.size() << " assets, " << data.snapshots.size() << " common timestamps\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
