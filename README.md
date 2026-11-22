# BacktestingEngine

A small C++20 backtesting engine for daily data: the generic parts only (data loading, orders and trades, the runners,
indicators and metrics). It contains no trading strategies; those live in a separate private project that uses this
engine as a library.

Build: `cmake -S . -B build && cmake --build build`. Tests: `./build/engine_tests`.
Program: `./build/backtester <folder with .csv files>` loads the files, lines the assets up and says what it found.

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
| Swap (overnight interest) | `swap.*` |
| Metrics | `metrics.*`: total return, CAGR, max drawdown, Sharpe, Sortino, MAR (CAGR / max drawdown), win rate, profit factor, expectancy |
| Indicators | `indicators/core.hpp`, `indicators/atr.hpp` |

Every part has its tests in `tests/`.

## Working rules
- Tests first, then the code; check the tests by breaking the code on purpose.
- Fix parameters before looking at results; count every idea tried (the more ideas tried on the same data, the more
  a good-looking result is luck).
- Compare a strategy with random-direction runs of the same system before believing it.
