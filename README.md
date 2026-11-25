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

## The local server
`./build/server <data folder> <runs folder> [--web <folder>] [--port <n>]` serves the API on `127.0.0.1` (this computer
only; the default port is 8080). With `--web` it also serves the built web app at `/`. Every run is saved as
`<runs folder>/<id>.json`; ids count up (`000001`, `000002`, ...), so the folder is also the count of everything tried.

| Request | Answer |
|---|---|
| `GET /api/strategies` | the strategies and their parameter schemas (default, min, max, integer) |
| `GET /api/datasets` | the file names in the data folder |
| `GET /api/layouts` | the ways a data file can be read: `[{name, builtin, description}]`, `auto` first (see "Data layouts") |
| `POST /api/runs` | `202 {"id": "000001"}`; the body is the same request the `runner` takes. A bad request is `400 {"error": ...}` |
| `POST /api/sweeps` | `202 {"id": ...}`; a density sweep (see below). A bad request is `400` |
| `POST /api/portfolios` | `202 {"id": ...}`; a portfolio (see below). A bad request is `400` |
| `POST /api/runs/{id}/cancel` | Cancels a run. A queued run of any kind is cancelled at once (`200`); a running sweep stops at its next step (`202`) and ends as `cancelled`. A finished run, or a single run or portfolio that is already running, is `409`; an unknown id is `404`. A cancelled sweep still counts as ideas tried |
| `GET /api/runs/{id}` | the run: `status` (`queued`, `running`, `done`, `failed`), `spec`, and `result` or `error` |
| `GET /api/runs` | one summary per run, newest first (headline metrics only) |

    curl -X POST localhost:8080/api/runs -d '{"strategy": "buy_and_hold", "dataset": "btc.v.txt", "cash": 5000}'
    curl localhost:8080/api/runs/000001

A run found `queued` or `running` when the server starts was cut off by a stop and is marked `failed`. A private project
calls `runServer(registry, options)` from its own `main()` after adding its strategies to the registry.

## The web app
`web/` is a Svelte + Vite app (charts by ECharts) that talks to the server's API: pick a dataset and a strategy, fill in
the parameters (the form is built from `/api/strategies`, with each parameter's limits checked as you type), run, and see
the metrics, the equity curve with its drawdown, the trades and the history of every run. The **Density sweep** tab runs a
sweep (see below): you choose two axes and every setting of the test, must write your pass rule in a required note first,
and get the real densities against the shuffled copies, a heatmap of the grid and a histogram of the copies. The history
keeps a count of the ideas tried per dataset. The **Portfolio** tab runs several sleeves side by side (add a sleeve per
dataset, each with its own strategy, settings and cash) and shows each sleeve and the whole portfolio in a table, the
sleeves' equity stacked with the portfolio's drawdown, and the trades by sleeve. Node is only needed to build it.

    cd web && npm install            # once
    npm test                         # the formatting, parameter and API-call logic
    npm run build                    # writes web/dist
    cd .. && ./build/server ../data runs --web web/dist       # then open http://127.0.0.1:8080

While working on the app, run the server (without `--web`) and `npm run dev` in `web/`: the page is at
http://localhost:5173 and `/api` is forwarded to the server on port 8080.

## Data layouts

A request names how its dataset file is read with an optional `"layout"` (a sleeve has its own). `auto`, the default, reads
a file whose header has `<DATE>` as `stooq` and any other file as `simple`. The built-ins are:

- `simple`: `timestamp,open,high,low,close` with a millisecond timestamp.
- `stooq`: `<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>`, dates like `20100717`.

For another format, put a `layouts.json` in the data folder. It is read again on every use, so an edit needs no restart.
Each entry is a name and the `CsvLayout` fields (see `include/loader.hpp`); every field is optional and defaults to `simple`:

    { "my_exchange": { "description": "semicolons, ISO dates", "delimiter": ";", "hasHeader": true,
                       "timeFormat": "isoDate", "time": "Day", "open": "Open", "high": "High", "low": "Low",
                       "close": "Close", "volume": "Volume" } }

`timeFormat` is `raw`, `epochSeconds`, `compactDate` or `isoDate`; a column is its header text or its 0-based position.
A name may not be a built-in name. A mistake in the file (bad JSON, an unknown key, a wrong type) is reported with the
entry's name, and until it is fixed requests fail with that message. The layout used is saved in the run's `spec`.
The web app shows a Layout dropdown beside every dataset.

## Density sweeps
`POST /api/sweeps` runs the density test of the research program `density_sweep` without recompiling: the strategy over a
grid of two of its parameters, on the real data and on K shuffled copies of it. Every choice that can change the result
is part of the request, and none is picked for you: `strategy`, `dataset`, `axes` (two, each `param`, `from`, `to`,
`step`), `metric` (`mar`, `cagr`, `sharpe`, `sortino`, `total-return`, `profit-factor`, `expectancy`), `threshold`,
`radius`, `minTrades`, `nullRuns`, `block` and a **`note`** that says what you expect and what counts as a pass: the
request is refused without it. `params` (fixed values for the parameters that are not swept), `cash` and
`periodsPerYear` are optional. Every axis value is checked against the parameter's limits before anything runs.

The result is numbers only: the real pass, plateau and peak-over-neighbours densities, the shuffled densities (median,
95th percentile, how many copies reached the real value, every copy's value), one metric per cell, and **ideas tried**
(cells x (1 + K)). Whether it counts as a pass is for you to judge against your own note. A sweep appears in the same
history as the single runs (`kind: "sweep"`), reports its progress while it runs and is logged the same way. At most
50,000 cells and 2,000,000 backtests per sweep.

## Portfolios
`POST /api/portfolios` is the job version of the research program `portfolio_run`: `sleeves` is a list of
`{name, dataset, strategy, params, cash}`; every sleeve is one dataset with its own strategy settings and its own bucket of
cash, and nothing moves between sleeves. A sleeve's name must be unique; two sleeves may use the same dataset under
different names. Options: `commonPeriodOnly` (start on the first day every asset has data, otherwise a late asset sits in
cash and dilutes the result) and `periodsPerYear`. The result has the combined and per-sleeve equity curves, each sleeve's
metrics against its own cash, the portfolio's against the sum of the cash, and the trades tagged with their sleeve. A
portfolio counts as one idea in the history. There are no spreads or swap yet.

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
| Local web server | `run_store.*`: one JSON file per run in a folder (the log of everything run). `run_manager.*`: runs queued backtests one at a time on a worker thread. `api.*`: the whole HTTP API as one testable function. `http_server.*`: the socket layer (cpp-httplib) and `runServer` |
| Density sweep job | `sweep_job.*`: `runSweep` runs a strategy over a grid of two parameters on the real data and on shuffled copies (seeds 1..K, several at a time, same answer however many threads), and returns the real densities, the shuffled densities and the grid. `sweep_job_json.*`: the same in JSON |
| Portfolio job | `portfolio_job.*`: `runPortfolio` runs several sleeves side by side (each one dataset with its own strategy, settings and cash; nothing moves between sleeves) and returns each sleeve's curve and metrics, the combined curve and metrics (against the sum of the cash) and the trades tagged by sleeve. `portfolio_job_json.*`: the same in JSON |
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
