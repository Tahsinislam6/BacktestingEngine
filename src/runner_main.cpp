#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "buy_and_hold.hpp"
#include "job.hpp"
#include "job_json.hpp"

// Runs one job described in JSON, so no recompiling is needed to change a setting.
//
//   runner <data folder> <job.json>    run the job, print the result as JSON
//   runner <data folder> -             the same, reading the job from standard input
//   runner <data folder> --list        print the strategies (with their parameters) and the datasets
//
// This program knows only the engine's built-in strategies. A private project makes its own program that adds its strategies
// to the registry first (see registerBuiltins in buy_and_hold.cpp for how one is added).
int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: runner <data folder> <job.json | - | --list>\n";
        return 2;
    }

    try
    {
        StrategyRegistry registry;
        registerBuiltins(registry);
        const std::string dataDir = argv[1];
        const std::string what = argv[2];

        if (what == "--list")
        {
            const nlohmann::json doc = {{"strategies", strategiesToJson(registry)}, {"datasets", listDatasets(dataDir)}};
            std::cout << doc.dump(2) << "\n";
            return 0;
        }

        std::string text;
        if (what == "-")
            text.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        else
        {
            std::ifstream in(what);
            if (!in)
                throw std::runtime_error("cannot open " + what);
            text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::cout << runJobJson(registry, dataDir, text) << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
