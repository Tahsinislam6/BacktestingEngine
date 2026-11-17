# BacktestingEngine: building an NNFX algorithm honestly

A C++20 backtesting engine for daily forex data. Its purpose is to find out, with as little self-deception as possible,
whether the **No Nonsense Forex (NNFX)** approach, or any part of it, has a real edge, and to build an algorithm
only from the parts that survive testing.

Build: `cmake -S . -B build && cmake --build build`. Tests: `./build/engine_tests`.
Commands and code conventions are in [CLAUDE.md](CLAUDE.md). This file explains the *why*.

---

## 1. The NNFX strategy

NNFX is a rule-based daily-chart system. Decisions are made once per day, after the close, so there is no screen
watching and little discretion. A trade needs several indicators, each with one job.

| Part | Job | Example here |
|---|---|---|
| **Baseline** | The trend line. Price crossing it is the entry signal. Above = long, below = short. | Kijun, EMA, SMA, HMA, ... (28 in `include/baselines/`) |
| **C1** (first confirmation) | Must agree with the trade direction. | RSI, Aroon, Bears-Bulls-Impulse |
| **C2** (second confirmation) | A second, different confirmation. | same pool as C1 |
| **Volume / filter** | Skips entries in dead or unsuitable markets. | atr_expansion |
| **Exit indicator** | Closes the trade when it turns against it. | Chandelier Exit |
| **ATR** | Sets stop, target, position size and the "don't chase" rule. | 14-period ATR |

### The trade rules (all implemented in `--mode nnfx`)
1. Signal: the close crosses the baseline. The confirmations may agree within a 7-bar window of the cross.
2. Don't chase: skip the entry if price is already more than 1 ATR beyond the baseline.
3. Risk 1% of the account per trade half. The stop is 1.5 ATR away and shared by both halves.
4. Two half-size trades. Half takes profit at 1 ATR. After that the runner's stop moves to breakeven.
5. The runner then trails behind the best close, until the stop, the exit indicator, or a baseline cross back.
6. Extras: one-candle rule, continuation trades, a cap on total open risk, a cap per currency.
7. Costs: the broker's spread on every fill (Pepperstone spread-betting list in `costs.csv`). Swap/financing is not modelled.

Stops are real stop orders: they are moved with `MoveStop` orders and fill at the stop price, or at the open on a gap.
Results are measured in **R** (multiples of the initial risk), so they do not depend on account size.

---

## 2. The strategy for building an algorithm from it

NNFX is a *recipe*; whether any ingredient helps is an empirical question. Choosing indicators by eye, or
trying many and keeping the best, produces systems that look great on past data and fail afterwards. The
method below exists to prevent that.

### 2.1 Ground rules (never broken)
1. **Split the data.** `../data/in-sample` is for choosing and tuning, `../data/out-of-sample` is for judging.
   Each is 10 pairs of daily data, 2016 to 2025. Only the out-of-sample columns count.
2. **Write the pass rule before running** and record it with `--note`. Examples: "out-of-sample expectancy at least
   0.03R and beats 90% of random runs".
3. **One logged run per question.** No re-running with tweaked settings. Every test is appended to
   a csv in `results/` (`--log-dir results --note "..."`).
4. **No tuning on the result.** Component parameters are fixed to their defaults. Thresholds are never moved after
   seeing a result.
5. **Fix everything except the thing being tested.** The baseline stays fixed (Kijun) while a confirmation is tested.
6. **Remember multiple testing.** Test many things and about 1 in 10 passes a 90% bar by luck. A near-pass is a lead
   that must replicate on other data, never a finding.

### 2.2 Controls: "better than random?"
A positive result means little unless it beats a random stand-in.
- **Direction control** (`--control N`): the same trades with random long/short directions.
  If the baseline's direction is not better than coin flips, it has no information.
- **Component control** (`--component-control N`): the real component against no component, and against N
  random components with the same state frequency and persistence (a Markov chain that knows nothing about prices).
- **Entry-level control** (`--entry-control N`): the same question, but measured on the entries alone, so stops,
  sizing and position limits cannot hide or mimic an edge.

### 2.3 Order of work
1. **Engine fidelity.** Tests first, then a mutation check (break the code on purpose and confirm a test fails).
   Realistic exits, movable stops, real spreads.
2. **Baselines** (`--screen`): all 28, judged out-of-sample against random directions.
3. **Entry study** (`--entry-study N`): do baseline crosses carry any forward information? It measures
   forward moves at 1 to 40 bars in ATR units, net of spread, a target-vs-stop race, and the best/worst excursion,
   against a coin-flip direction. It involves no stops or sizing.
4. **Components one at a time**, on the fixed baseline: C1, then C2, then filter, then exit.
5. **Other signal types** (`--signal breakout:20`, `--signal momentum:60`): the same entry study without a baseline.
6. **Only things that pass the preset rule** go on: replicate on new data, forward-test, final holdout.

### 2.4 Where we are (results so far)
- 28 baselines screened: none showed a replicable edge.
- Components tested (RSI, Aroon, Bears-Bulls-Impulse, atr_expansion, Chandelier Exit): none improved entries or results.
  One near-pass (Bears-Bulls-Impulse) did not replicate on EMA/SMA.
- Entry study across all 28 baselines: no baseline cross carries replicable forward information.
- Signal types (breakouts 20/55, momentum 20/60/120): none passes the rule on both samples. Momentum 20 looked good
  out-of-sample only (gain about 0.01 ATR net of spread), and it did not appear in-sample. Breakouts tend to
  *reverse* over 20 to 40 bars; that is a hypothesis, not a finding.
- The whole NNFX base system earns about 0.05R per trade out-of-sample. That is inside the noise of about ±0.04R
  at roughly 900 trades.

**Conclusion so far:** price-based daily entries on these 20 pairs over 2016 to 2025 carry essentially no replicable
forward information. The engine is trustworthy; the strategy has not yet earned any trust.

### 2.5 What would change the picture
- More independent data: 2006 to 2015 for the same pairs, then more pairs. Any hypothesis is registered first and
  run once.
- A different edge source: a mean-reversion (fade) test of breakouts, volume data for volume filters, other
  timeframes.
- Financing (swap) costs, and spreads that depend on time of day, to make the cost model more realistic.
- Final steps for anything that survives: a demo forward test, then a one-time final holdout.

---

## 3. Adding your own indicator (AI-friendly)

Baselines and components are single self-contained headers; no other file needs editing.
- Baseline: copy `templates/baseline_template.hpp` to `include/baselines/<name>.hpp`.
- Confirmation, exit or filter: copy `templates/component_template.hpp` to `include/components/<name>.hpp`.
- Build and run `./build/engine_tests`. The generic tests check the contract, and a hand-worked-numbers test is
  optional for non-obvious indicators.

## 4. Typical commands

```
# screen every baseline, judged out-of-sample
./build/backtester --screen --insample ../data/in-sample --outsample ../data/out-of-sample \
    --costs costs.csv --log-dir results --note "rule: OOS exp >= 0.03R and beats >= 90%"

# does a component add anything over random stand-ins at the entry level?
./build/backtester --baseline kijun --mode nnfx --confirm aroon --entry-study 1000 --entry-control 200 \
    --insample ../data/in-sample --outsample ../data/out-of-sample --costs costs.csv \
    --log-dir results --note "rule: ..."

# signal study without a baseline
./build/backtester --signal momentum:20 --entry-study 1000 \
    --insample ../data/in-sample --outsample ../data/out-of-sample --costs costs.csv
```

Read `--help` for every option. All logs are in `results/` (ignored by git, as are the baselines and components).
