# BacktestingEngine

C++20, CMake + GoogleTest. Build: `cmake -S . -B build && cmake --build build`. Tests: `./build/engine_tests`.
Run: `./build/backtester --insample <folder> --outsample <folder> --baseline <name> [--mode flip|nnfx]` (`--help` lists everything).
Modes: `flip` = always in the market; `nnfx` = baseline-cross entries, ATR stop/target, runner with breakeven + trailing exit.
Control: `--mode nnfx --no-cross-exit --control 200` compares the winner's direction with 200 random-direction runs.

## Adding a baseline indicator (the only thing most tasks need)

1. Copy `templates/baseline_template.hpp` to `include/baselines/<name>.hpp`.
   `<name>` is lowercase letters, digits and underscores; it becomes the `--baseline` name.
2. Fill in the three marked places: the namespace `baselines::<name>`, the indicator class, and the `Spec` struct
   (`Line`, `ranges()`, `make(params)`, `warmup(params)`).
3. Build and run the tests. That is all.
   - Do NOT edit `main.cpp`, `CMakeLists.txt` or any registry: CMake finds every header in `include/baselines/`
     and generates the list; `tests/baselines_test.cpp` checks every baseline automatically.
   - Do NOT add a test file unless you want to check the maths against hand-worked numbers (see
     `tests/kijun_test.cpp` for the style). One small file at most, and only for non-obvious indicators.

Indicator rules (the generic tests enforce them):
- `update(const Bar&)` once per bar, oldest first; use only that bar and earlier ones. Any bar fields are fine.
- `ready()` is false until exactly `warmup(params)` bars were seen, then stays true.
- `value()` is `const`, returns the line's price, and throws `std::logic_error` when not ready. It must not
  change state, and the value must be finite (guard divisions by zero, e.g. flat prices).
- On a flat market at price P the line equals P. Copies must be independent (no statics, no globals).
- Searching a parameter range is the optimiser's job: `ranges()` is only the default search; keep the
  default grid under about 1000 sets and warm-up under 1000 bars.
- Shared helpers (ATR, warm-up counter, `PerAsset`) live in `include/indicators/`. Do not put a baseline there.

## Screening baselines (the loop to follow for each new indicator)

1. Add the baseline file (above), build, run `./build/engine_tests`; fix anything the generic tests flag.
2. Run the screen over every baseline in one go:
   `./build/backtester --screen --insample <in> --outsample <out> --costs costs.csv --screen-csv screen.csv`
   (about a second for a handful of baselines). One row per baseline, best first.
3. Judge ONLY the out-of-sample columns. A `candidate` has out-of-sample expectancy >= 0.03R and beats >= 90% of
   200 random-direction runs; change the bars with `--min-expectancy` / `--min-beats`. In-sample columns are
   biased (the winner was tuned on that data).
4. Log each result (baseline, params, OOS exp R, beats). With many baselines some look good by luck, so only a
   clear margin counts, and anything shortlisted still needs confirmation on data it was not chosen on.
5. Never tune the thresholds or the parameter ranges after seeing a baseline's out-of-sample result.

`--screen` takes no `--baseline`/`--mode`: it always trades nnfx without the cross-back exit. For a closer look at one
baseline use `--baseline <name> --mode nnfx [--no-cross-exit --control 200]`.

## Layout
- `include/baselines/`   one self-contained header per baseline
- `include/indicators/`  shared building blocks (core.hpp, atr.hpp)
- `include/*.hpp`, `src/*.cpp`  engine, strategies (flip, nnfx), optimiser, validation, reports, CLI
- `tests/*_test.cpp`     picked up automatically; no CMake edit needed
- `templates/`           the baseline template (not compiled)

## Conventions
- Data: daily FX CSVs, millisecond timestamps (`timestamp,open,high,low,close,...`), one file per pair.
- Costs: per-pair spread in pips, applied as half the spread on every fill (`costs.hpp`, `costs.csv`).
- Write tests first and check them by breaking the code on purpose (mutation check).
