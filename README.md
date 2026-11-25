# BacktestingEngine

A small C++20 backtesting engine for daily data: the generic parts only (data loading, orders and trades, the runners,
indicators and metrics). It contains no trading strategies; those live in a separate private project that uses this
engine as a library.

Build: `cmake -S . -B build && cmake --build build`. Tests: `./build/engine_tests`.
Program: `./build/backtester <folder with .csv files>` loads the files, lines the assets up and says what it found.

## Running a job without recompiling
`./build/runner <data folder> <job.json>` (or `-` to read the job from standard input) runs one backtest and prints the
result as JSON; `./build/runner <data folder> --list` prints the strategies with their parameters, and the datasets.

    echo '{"strategy": "buy_and_hold", "dataset": "btc.v.txt", "params": {"fraction": 1}, "cash": 5000, "periodsPerYear": 365}' \
      | ./build/runner ../data -

Only `strategy` and `dataset` are required; any other unknown key is an error. A private project adds its own strategies
with `registry.add(info, builder)` in its own `main()` and uses the same `runJobJson`, so no strategy has to live here.

## Using it from another project
The CMake target `engine` is the library; its headers are in `include/`. From another project's CMakeLists.txt:

```cmake
include(FetchContent)
FetchContent_Declare(engine GIT_REPOSITORY https://github.com/Tahsinislam6/BacktestingEngine.git GIT_TAG main)
FetchContent_MakeAvailable(engine)
target_link_libraries(my_program PRIVATE engine)
```

Use a commit hash instead of `main` to pin a version. To build against a local copy of the engine instead of GitHub,
configure with `-DFETCHCONTENT_SOURCE_DIR_ENGINE=/path/to/BacktestingEngine`. The program and the tests are only
built when the engine is the top-level project.

## What is here
| Part | Files |
|---|---|
| Bars and CSV loading | `bar.hpp`, `loader.*`, `timestamp.*` |
| Aligned multi-asset data | `aligned.*` |
| Orders, trades, portfolio | `order.hpp`, `trade.hpp`, `portfolio.*` |
| Strategy interface and runners | `strategy.hpp`, `backtester.*` (one asset), `multi_backtester.*` (many assets, spreads, swap) |
| Sleeves (separate buckets of money) | `sleeve.*`: `runSleeves` gives each sleeve its own assets, strategy and cash, runs it alone and adds the results up; `computeSleeveMetrics` for each sleeve and the whole portfolio. `single_asset.*`: `SingleAssetAdapter` runs a one-asset `Strategy` inside a sleeve |
| Swap (overnight interest) | `swap.*` |
| Metrics | `metrics.*`: total return, CAGR, max drawdown, Sharpe, Sortino, MAR (CAGR / max drawdown), win rate, profit factor, expectancy |
| Indicators | `indicators/core.hpp`, `indicators/atr.hpp` |
| Parameter sweeps | `parameter_grid.*`: run any strategy over a grid of parameter values (any number of axes) and keep each run's metrics |
| Parametric density | `density.*`: how much of the grid gives good results (pass density, plateau density, peak over neighbours, percentile) |
| Job runner (for the web app) | `registry.*`: `StrategyRegistry`, the table of strategies a job can name, with each parameter's default and limits. `buy_and_hold.*`: the one built-in strategy (the baseline). `job.*`: `runJob` runs one backtest from a plain description (strategy, parameters, dataset file name, cash); `listDatasets` / `loadDataset` read a data folder (Stooq files and `timestamp,open,high,low,close` files). `job_json.*`: the same in JSON, the text the HTTP server and the web app will exchange |
| Luck baseline | `null_series.*`: `shuffleReturns` makes a block-resampled copy of a series with the real order destroyed |

Every part has its tests in `tests/`.

## Sleeves
A sleeve is a bucket of money with its own assets, its own strategy and its own cash; nothing moves between sleeves. This
lets different strategies trade different assets side by side and be judged both separately and as one portfolio.

1. `runSleeves(data, sleeves, spreads, swap)` cuts the aligned data into one slice per sleeve (every slice keeps all the
   snapshots, so the curves line up), runs each slice with `runMultiBacktest` and adds the results up. The combined
   equity curve is the sum of the sleeves' curves; the combined trades are tagged with their sleeve and carry the asset's
   position in the full data. An asset may be in only one sleeve; every configuration mistake throws before anything runs.
2. A sleeve's strategy is a `MultiStrategy`. A plain one-asset `Strategy` goes in a one-asset sleeve wrapped in
   `SingleAssetAdapter` (it refuses more than one asset, because it would size and trade them as one).
3. `computeSleeveMetrics(result, periodsPerYear)` measures each sleeve against its own cash and the portfolio against
   the sum of the cash.

## Parametric density
A strategy that only works at one exact setting is probably fitted to the past; one that works over a broad region of
its parameters is more believable. The three parts above measure that:

1. `sweep(series, axes, factory, cash)` runs the strategy once per combination (the factory builds a fresh strategy from
   the parameter values) and returns one `Metrics` per cell.
2. `toDensityGrid(result, metric, minTrades)` picks one number per cell and marks cells with too few trades invalid.
   `plateauDensity(grid, threshold, radius)` is the share of cells whose whole neighbourhood passes (a lone lucky cell
   scores nothing); `passDensity` is the plain share of passing cells; `peakOverNeighbours` shows a spike.
3. Run the same sweep on K `shuffleReturns` copies of the data (seeds 1..K) and take `percentile(densities, 0.95)`: the
   real density only means something if it beats that.

Fix the axes, the metric, the threshold, the radius and `minTrades` before looking at any result, and count the cells
(every one is an idea tried on that data).

## Working rules
- Tests first, then the code; check the tests by breaking the code on purpose.
- Fix parameters before looking at results; count every idea tried (the more ideas tried on the same data, the more
  a good-looking result is luck).
- Compare a strategy with random-direction runs of the same system before believing it.
