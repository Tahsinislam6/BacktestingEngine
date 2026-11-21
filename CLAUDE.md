# BacktestingEngine

FIRST read `.claude/memory/INDEX.md` (decisions, findings so far, data, phase-one plan, working style). After each logged test
add a row to `.claude/results/tries_ledger.md`; after each decision or finding update one note in `.claude/memory/`.

C++20, CMake + GoogleTest. Build: `cmake -S . -B build && cmake --build build`. Tests: `./build/engine_tests`.
Run (a SYSTEM: a baseline with fixed parameters plus at least one component; nothing is searched or tuned, and a baseline
is never tested alone): `./build/backtester --insample <folder> --outsample <folder> --baseline kijun --params period=26
--confirm aroon` (`--help` lists everything). Without `--params` or without a component the program refuses to run.
`--params` names every parameter of the baseline, each inside the baseline's `ranges()` (which now only say what is legal).
A run prints the metrics (return, Sharpe, drawdown, trades, capture), the per-asset table and the R-multiples for the
in-sample and the out-of-sample data. Choosing which baseline, parameters and components to try is up to you (or the
automation), fixed in advance and written in `--note`; the engine never searches.
Exits are real stop orders: after the target half is taken the runner's stop is MOVED (`OrderType::MoveStop`) to its
entry price and then trailed behind the best close; it fills at the stop (or the open on a gap) on a later bar. R is
always measured from the stop the trade opened with (`Trade::initialStop`). Log rows made before this change (no
`stops=orders` in the setup column) used close-only checks and are not comparable with the new ones.
Control: `--no-cross-exit --control 200` compares the system's direction with 200 random-direction runs.
There is no flip mode, no parameter search, no walk-forward and no baseline screen any more (removed on 2026-11-17; the old code
is in git history).

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
- `ranges()` says which values are legal for `--params` (nothing searches it); keep warm-up under 1000 bars.
- Shared helpers (ATR, warm-up counter, `PerAsset`) live in `include/indicators/`. Do not put a baseline there.

## Adding a confirmation, exit or filter component

Each component has its OWN FOLDER with its tests, and the whole `include/components/` folder is git-ignored (local only):
`include/components/<name>/<name>.hpp` and `include/components/<name>/<name>_test.cpp`. Copy `templates/component_template.hpp`
to the first path, fill in the three marked places, write the tests (hand-worked numbers from the MT4 source), build, run the
tests. CMake finds the header and the tests; `tests/components_test.cpp` checks every component generically (ready exactly after
its warm-up, valid values on every kind of data, a rising market is never bearish, copies independent) - so a new indicator must
pass that too. In the header, `#include "../../components.hpp"`; in the test, `#include "components/<name>/<name>.hpp"`.
- A **State** component (`state()` = -1/0/+1) can be a confirmation (`--confirm <name>`, must agree with the trade's
  direction at entry, within `--confirm-window` bars of the baseline cross) or an exit indicator
  (`--exit-indicator <name>`, closes a trade when it turns to the opposite state).
- A **Gate** component (`allows()`) is a filter (`--filter <name>`): the entry is only taken when it allows.
- Component parameters are FIXED (`defaults()`), never searched: they add no tuning freedom.
- Other nnfx switches: `--one-candle`, `--continuation`, `--max-open-risk 0.06`, `--max-per-currency 2`.
- `--no-scaling`: one trade per signal with the whole 2% risk and no take-profit half; no breakeven; the stop trails 1.5 ATR behind the best close from the bar after entry (logged as `no-scaling` in the setup column). Plan B with it (2026-11-18): 0 finalists, mean OOS -0.0525R.
- `--stop-atr <x>` (default 1.5), `--atr-period <n>` (14), `--risk <x>` (risk of the whole signal, default 0.02), `--no-trail` (no trail, no breakeven) and, for the grid, `--cross-exit` (close on the next baseline cross; the random-direction control closes at the next cross either way; the grid runs WITHOUT the cross-back exit unless this is given). a plan line `c1 none` (no confirmation, the cross alone; plan H, the 55-day breakout with no C1, ran 36 pairs: 0 finalists), `--max-distance <x>` / `--no-chase-limit` (the don't-chase limit, default 1 ATR; plan G, the 55-day breakout, ran 1,066 pairs without it: 0 finalists), `--c1-exit` (grid only: each pair's C1 also closes a trade when its state turns against it; the random control reads the C1 against the SIGNAL's direction, not the random trade's; run on plan B 2026-11-18: plain -0.031R and default -0.031R mean OOS, both 0 finalists; any further run is a new counted try). The user's plain strategy = `--no-scaling --no-trail --stop-atr 2.5 --atr-period 20 --risk 0.0025 --cross-exit`: plan B 2026-11-18, 0 finalists, mean OOS -0.0369R, mean beats 9.9%.
- `--trail-after <x>`: as `--no-scaling`, but the stop stays at its start until a close is x times the ATR at entry in profit, then trails 1.5 x the ENTRY ATR behind the best close (the user's rule). Plan B with x = 2 (2026-11-18): 0 finalists, mean OOS -0.0671R.
Example: `... --baseline kijun --params period=26 --confirm rsi --filter atr_expansion --max-per-currency 2`
Current components (19): State: adx_di, andean_oscillator, aroon, bears_bulls_impulse, chandelier_exit (an exit), cmo,
disparity_index, hlc_trend, laguerre, lwpi, pmo, rsi, tii, ttf. Gate (filters): adx_strength, atr_expansion, normalized_volume,
ttms, volatility_ratio. (Every added indicator raises the number of tries a future test must count; the phase-one plans were
frozen with 8 and 13 C1s.)

## Volume (added 2026-11-17)

`Bar::volume` is read from the data file's column named `volume` (any capitals, from the 6th column on; no such column = 0) by
`loadCsv`; folding a Sunday bar into Monday adds the two volumes; it flows unchanged through the aligned snapshots. It is an
FX feed's tick/size volume: the scale drifts a lot over the years (e.g. CHFJPY's median 504k in 2006 vs 122k in 2025, with
spikes of 31M), so only a bar's volume relative to its own recent average means anything. Real-volume components so far:
`normalized_volume` (a gate: volume above `level` percent of its `period`-bar average). The other new gates (`volatility_ratio`,
`ttms` squeeze, `adx_strength`) are volatility/trend-strength gates computed from price, which is what NNFX's "volume"
indicators mostly are. A gate is used with `--filter <name>`.

Testing a component (does it add anything?): add `--component-control 100`. For each chosen component it runs the
system with the real component, without it, and with 100 random stand-ins that show the same state frequencies and
persistence (a Markov chain measured on the data; it knows nothing about prices), one role at a time with the others
left as given. Read the out-of-sample "it beats" column: ~50% = no better than a random component. When the random arms trade rarely their expectancy is noisy, so
look at total R / Sharpe and the trade counts too. Like every other test, log it and never tune on the result.

## Results log (one csv per type of test)

Add `--log-dir results [--note "why"]` to `--control N`, `--component-control N`, `--entry-study N` or `--entry-control N`.
Rows are only ever appended (header written once), one file per test type in that folder:
`direction_control_log.csv` (one row per data set), `component_control_log.csv` (one row per component per data set),
`entry_study_log.csv` (one row per data set), `entry_control_log.csv` (one row per component per data set). Every row
starts with timestamp, note, setup (the nnfx switches), costs and the data folders, then the baseline, its params and
the numbers. Always pass `--note` saying what the test was for and what you decided in advance (the pass rule). A log
file with other columns than the program writes is refused: move it aside to start a new one. Nothing is tuned any
more, so the in-sample and out-of-sample rows are both fair; judge a result on data the system was not chosen on.
(`screen_log.csv` is the old batch screen's log; the program no longer writes it.)

## Entry study (does the baseline's signal carry any information?)

`--entry-study 1000 [--log-dir results --note "..."]` (on the system's baseline signals): for every
baseline cross the don't-chase filter lets through it measures where price goes 1, 2, 3, 5, 10, 20 and 40 bars later
in ATR units in the signal's direction (gross and net of the spread), the share that moved the right way, a
target-vs-stop race (target 1 ATR, stop 1.5 ATR, 40 bars, stop first on a tie) and the best/worst excursion over 20
bars. Everything is compared with the same signals given a coin-flip direction ("beats" = share of random runs it is
better than). No stops, sizing or position limits are involved and overlapping signals all count, so it shows whether
the entry has any edge before the exits can hide or mimic one. Set the reading rule
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

## Testing systems (baseline + components)

The unit of testing is a system, never a baseline alone: a baseline with fixed parameters plus at least one component.
Typical order (each choice made in advance and written in `--note`; one logged run per question; never tune on a result):
1. Fix a baseline and its `--params`, and a neutral stand-in for the component you are not testing yet.
2. Run the system with each candidate component (`--confirm`, `--filter`, `--exit-indicator`) and judge it with
   `--entry-control` / `--component-control` against random stand-ins, not by raw expectancy (fewer trades = noisier).
3. Keep the best by a score fixed in advance, then vary the baseline with that component fixed. Every try counts: with M
   candidates tried, the chance that one beats 90% of its own random runs by luck is 1 - 0.9^M (95% at 28), so keep a
   running count of tries and confirm any finalist ONCE on data nothing was chosen on.
Data folders (made outside the project, same 20 pairs): `../data/post2016-in-sample` and `../data/post2016-out-of-sample`
(2016-2025, heavily used: fine for finding candidates, not for confirming them); `../data/pre2016-in-sample`,
`../data/pre2016-out-of-sample` and `../data/pre2016-all` (2006-2015, used so far only for four logged holdout tests of
2026-11-17). Keep a block (e.g. 2006-2010) untouched until a finalist exists. Every new candidate run on a holdout uses
some of it up.

## The grid (phase one: every baseline x C1 pair in one batch)

`./build/backtester --grid phase1/plan.txt --insample ../data/post2016-in-sample --outsample ../data/post2016-out-of-sample
--holdout ../data/pre2016-all --costs costs.csv --log-dir results --note "<the pass rule, written BEFORE the run>"
--ledger .claude/results/tries_ledger.md [--prior-tries N] [--control 200] [--min-beats 0.9] [--min-expectancy 0.03]`
- Plan files: `phase1/plan.txt` (8 C1s, FROZEN, run twice), `phase1/plan_13c1.txt` (13 C1s, 364 pairs, FROZEN, run once),
  `phase1/plan_lwpi.txt` (FROZEN, run once on 2006-2015) and `phase1/plan_hlc.txt` (the hlc_trend(close_period=10) lead on 2006-2015: 33 pairs, FROZEN, did not replicate), `phase1/plan_sweep.txt` (parameter sweep: 130 baseline variants x 39 C1 variants = 5,070 pairs, FROZEN, run once 2026-11-18, 0 finalists; a plan may repeat a baseline with other parameters and a `c1` line may carry parameters, e.g. `c1 rsi period=7`, shown as `rsi(period=7)` in the log) and `phase1/plan_filters.txt` (13 C1s x 5 filters = 1,820 pairs, DRAFT until
  approved; counting M = 2,184 with `--prior-tries 364`). A plan may also list `filter <name>` lines (Gate components): every baseline x C1 is
  then run with EACH filter (no `filter` lines = no filter), the filter gates the pair AND its random-direction runs, and `grid_log.csv` has a
  `filter` column (the older log without it was moved to `results/grid_log_v1_no_filter_column.csv`). The header of a plan holds the pass rule
  and the counting of tries. A plan file pre-registers the run: `baseline <name> <params>` lines (parameters fixed, inside the
  baseline's ranges) and `c1 <name>` lines (State components). It is checked by a test; once the first real run is made it
  never changes. It is a DRAFT until the user approves it.
- Per pair (always without the cross-back exit): the system is run on the in-sample and the out-of-sample folder, and its
  out-of-sample expectancy is turned into a score z against 200 random-direction runs of the same system. The correction:
  adjusted beats = F(z)^M, F = the share of ALL pairs' random runs (pooled) that z is above, M = pairs tried + `--prior-tries`.
- Finalist: out-of-sample expectancy >= 0.03R, beats >= 90% of its own random runs, adjusted beats >= 90%, in-sample
  expectancy > 0. Each finalist is run ONCE on `--holdout` with the same parameters; the beats bar becomes 0.9^(1/F) when
  there are F > 2 finalists. CANDIDATE = finalist that passes there too. Non-finalists never touch the holdout. Without
  `--holdout` there are finalists but no candidates.
- `--log-dir` and `--note` are required (every pair tried is a try); rows go to `grid_log.csv` (one per pair, failures
  included). `--ledger` appends one summary row to the tries ledger. A pair that errors still counts in M.
- The table lists the best 15 rows plus every finalist and every failed pair; the full table is in `grid_log.csv`.
- About 45 seconds for 28 baselines x 8 C1 on 8 cores.

## Swap (financing), added 2026-11-17

Off by default; turn it on with `--rates ../data/rates.csv [--swap-markup <pct a year>]`. Holding a position over a night earns
the interest rate of the currency held and pays the rate of the currency owed: per night, in quote currency, notional x
(+- (base rate - quote rate) - markup) / 100 / 365 (+ for a long, - for a short), where notional = quantity x the previous
close. It accrues for every trade open at the end of the previous snapshot, per calendar night between the two snapshots
(rounded to whole days, so a weekend is 3 nights and a clock change does not matter), using the rate difference in force at
the previous snapshot. Swap is part of a trade's result: its pnl (so R-multiples), the equity, the per-asset table (`swap`
column, already inside `profit`), the direction control, component control and grid (all use the same options).
- Dukascopy has no swap history (only current rates and a policy page that says its swaps follow central-bank rates), so the
  history comes from FRED/OECD monthly interbank rates: `../data/make_rates.py` builds `../data/rates.csv` (currency,date,rate,
  annual percent, holding from that date). Known limits: monthly values; NZD is spliced from the call-money series (to
  2024-12) to the 3-month interbank series (from 2025-01); SINGAPORE IS MISSING, so USDSGD gets no interest (the program
  prints a WARNING); the CHF series sits about 0.2 points below SARON and has a one-month glitch in 2016; no broker markup
  unless `--swap-markup` is given (Pepperstone's real markup is unknown: stress it, e.g. 1 or 2).
- The entry study, `--entry-control` and `--signal` measure price moves in ATR and do NOT charge swap (they print a note).
- The 224-pair phase-one grid of 2026-11-17 was run twice: first WITHOUT swap (costs column `costs.csv x1, no swap`... older rows just say `costs.csv x1`), then WITH swap (`... swap ../data/rates.csv markup 0`). Both: 0 finalists.

## Layout
- `include/baselines/`   one self-contained header per baseline
- `include/components/<name>/`  one folder per confirmation / exit / filter component: its header and its tests (git-ignored)
- `include/indicators/`  shared building blocks (core.hpp, atr.hpp)
- `include/*.hpp`, `src/*.cpp`  engine, the nnfx strategy, controls, entry study, reports, CLI
- `tests/*_test.cpp`     picked up automatically; no CMake edit needed
- `templates/`           the baseline and component templates (not compiled)

## Conventions
- Data: daily FX CSVs, millisecond timestamps (`timestamp,open,high,low,close,...`), one file per pair.
  Lives next to the project, outside it: `../data/in-sample` and `../data/out-of-sample` (10 pairs each).
  Run from the project folder with `--insample ../data/in-sample --outsample ../data/out-of-sample`.
  (The old copy under ~/Downloads/Trading/API is identical; the first log rows still name that path.)
- Costs: per-pair spread in pips, applied as half the spread on every fill (`costs.hpp`, `costs.csv`).
- Write tests first and check them by breaking the code on purpose (mutation check).
