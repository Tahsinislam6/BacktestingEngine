# BacktestingEngine

A small C++20 backtesting engine for daily data, restarted from its bare-bones core.
The earlier NNFX testing platform (baselines, components, grid, random-direction control, plans, results and notes)
is kept in git on the branch `archive-nnfx`.

Build: `cmake -S . -B build && cmake --build build`. Tests: `./build/engine_tests`.
Program: `./build/backtester <folder with .csv files>` loads the files, lines the assets up and says what it found.

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
