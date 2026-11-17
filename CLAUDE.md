# BacktestingEngine

C++20, CMake + GoogleTest. Build: `cmake -S . -B build && cmake --build build`. Tests: `./build/engine_tests`.
Run: `./build/backtester --insample <folder> --outsample <folder> --baseline <name> [--mode flip|nnfx]` (`--help` lists everything).
Modes: `flip` = always in the market; `nnfx` = baseline-cross entries, ATR stop/target, runner with breakeven + trailing stop.
Exits are real stop orders: after the target half is taken the runner's stop is MOVED (`OrderType::MoveStop`) to its
entry price and then trailed behind the best close; it fills at the stop (or the open on a gap) on a later bar. R is
always measured from the stop the trade opened with (`Trade::initialStop`). Log rows made before this change (no
`stops=orders` in the setup column) used close-only checks and are not comparable with the new ones.
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

## Adding a confirmation, exit or filter component

Same idea as a baseline: copy `templates/component_template.hpp` to `include/components/<name>.hpp`, fill in the
three marked places, build, run the tests. CMake finds it and `tests/components_test.cpp` checks it automatically.
- A **State** component (`state()` = -1/0/+1) can be a confirmation (`--confirm <name>`, must agree with the trade's
  direction at entry, within `--confirm-window` bars of the baseline cross) or an exit indicator
  (`--exit-indicator <name>`, closes a trade when it turns to the opposite state).
- A **Gate** component (`allows()`) is a filter (`--filter <name>`): the entry is only taken when it allows.
- Component parameters are FIXED (`defaults()`), never searched: they add no tuning freedom.
- Other nnfx switches: `--one-candle`, `--continuation`, `--max-open-risk 0.06`, `--max-per-currency 2`.
- These work with `--mode nnfx` and with `--screen` (the same components are applied to every baseline).
Examples: `... --mode nnfx --confirm rsi --filter atr_expansion --max-per-currency 2`

Testing a component (does it add anything?): add `--component-control 100`. For each chosen component it runs the
winner with the real component, without it, and with 100 random stand-ins that show the same state frequencies and
persistence (a Markov chain measured on the data; it knows nothing about prices), one role at a time with the others
left as given. Read the out-of-sample "it beats" column: ~50% = no better than a random component. The in-sample
lines were tuned with the component, so ignore them. When the random arms trade rarely their expectancy is noisy, so
look at total R / Sharpe and the trade counts too. Like every other test, log it and never tune on the result.

## Results log (one csv per type of test)

Add `--log-dir results [--note "why"]` to `--screen`, `--control N` or `--component-control N`. Rows are only ever
appended (header written once), one file per test type in that folder:
`screen_log.csv` (one row per baseline), `direction_control_log.csv` (one row per data set),
`component_control_log.csv` (one row per component per data set). Every row starts with timestamp, note, setup (the
nnfx switches, objective, min trades), costs and the data folders, then the numbers. Always pass `--note` saying
what the test was for and what you decided in advance (the pass rule). A log file with other columns than the program
writes is refused: move it aside to start a new one. Read the `data = out-of-sample` rows; in-sample is tuned.

## Entry study (does the baseline's signal carry any information?)

`--mode nnfx --entry-study 1000 [--log-dir results --note "..."]` (after the usual study, on the winner): for every
baseline cross the don't-chase filter lets through it measures where price goes 1, 2, 3, 5, 10, 20 and 40 bars later
in ATR units in the signal's direction (gross and net of the spread), the share that moved the right way, a
target-vs-stop race (target 1 ATR, stop 1.5 ATR, 40 bars, stop first on a tie) and the best/worst excursion over 20
bars. Everything is compared with the same signals given a coin-flip direction ("beats" = share of random runs it is
better than). No stops, sizing or position limits are involved and overlapping signals all count, so it shows whether
the entry has any edge before the exits can hide or mimic one. Read the out-of-sample table only. Set the reading rule
in `--note` first (e.g. informative only if the out-of-sample mean is > 0 and beats >= 90% at several consecutive
horizons). Note the race: with a 1 ATR target and a 1.5 ATR stop even random directions win about 60% of races, so the
target half breaks even by construction. Logged in `entry_study_log.csv`.
Other signals need no baseline: `--signal breakout:20 --entry-study 1000` (the close breaks the previous 20 bars' high/low; a
same-way break counts only if the last one was more than 20 bars ago) or `--signal momentum:60` (the sign of the 60-bar
return flips). Nothing is tuned, so both the in-sample and the out-of-sample table are fair tests; the don't-chase filter
does not apply. The random-direction comparison is a coin flip per signal, which is slightly generous at 40 bars when
same-way signals can overlap.
With `--confirm` / `--filter` the study applies them to the signals the way the strategy does (a cross waits up to
`--confirm-window` bars for the confirmation; the entry bar is where the forward moves are measured from).
`--entry-control N` then asks, for each chosen confirmation / filter, whether its entries are better than those of N
random stand-ins with the same frequency and persistence (and than no component at all): means at each horizon, the
race R and the excursion balance, ranked among the stand-ins. This is a cleaner test than `--component-control`
because no stops, sizing or position limits sit between the component and the measurement. Logged in
`entry_control_log.csv`. Exit indicators are not entries, so they are refused.

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
- `include/components/`  one header per confirmation / exit / filter component
- `include/indicators/`  shared building blocks (core.hpp, atr.hpp)
- `include/*.hpp`, `src/*.cpp`  engine, strategies (flip, nnfx), optimiser, validation, reports, CLI
- `tests/*_test.cpp`     picked up automatically; no CMake edit needed
- `templates/`           the baseline and component templates (not compiled)

## Conventions
- Data: daily FX CSVs, millisecond timestamps (`timestamp,open,high,low,close,...`), one file per pair.
  Lives next to the project, outside it: `../data/in-sample` and `../data/out-of-sample` (10 pairs each).
  Run from the project folder with `--insample ../data/in-sample --outsample ../data/out-of-sample`.
  (The old copy under ~/Downloads/Trading/API is identical; the first log rows still name that path.)
- Costs: per-pair spread in pips, applied as half the spread on every fill (`costs.hpp`, `costs.csv`).
- Write tests first and check them by breaking the code on purpose (mutation check).
