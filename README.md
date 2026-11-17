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

### The trade rules (all implemented by the engine)
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
2. **Baselines**, all 28, judged out-of-sample against random directions (done earlier; the baseline-only screen has since been removed).
3. **Entry study** (`--entry-study N`): do baseline crosses carry any forward information? It measures
   forward moves at 1 to 40 bars in ATR units, net of spread, a target-vs-stop race, and the best/worst excursion,
   against a coin-flip direction. It involves no stops or sizing.
4. **Components one at a time**, on the fixed baseline: C1, then C2, then filter, then exit.
5. **Other signal types** (`--signal breakout:20`, `--signal momentum:60`): the same entry study without a baseline.
6. **Only things that pass the preset rule** go on: replicate on new data, forward-test, final holdout.

### 2.4 What a test is: a system, never a baseline alone
Every run trades one NNFX system: a baseline with fixed parameters (`--params`) plus at least one component
(`--confirm`, `--filter`, `--exit-indicator`). Nothing is searched or tuned, and the program refuses a baseline-only run.
The plan is to take a neutral baseline with a neutral C1, test other C1s on it, keep the best by a score fixed in
advance, then test that C1 with other baselines, and so on. Because every round adds tries, count them: with M candidates
tried, the chance one beats 90% of its own random runs by luck is 1 - 0.9^M (95% at 28). Judge candidates against
random stand-in components (`--component-control`, `--entry-control`), not by raw expectancy, and confirm a finalist once
on data nothing was chosen on. Data folders: `../data/post2016-*` (2016-2025, for finding), `../data/pre2016-*`
(2006-2015, the holdout); keep a further block (say 2006-2010) back for the very last check.

### 2.5 Where we are (results before the fresh start; the raw logs were cleared, summary in `.claude/memory/findings-so-far.md`)
- 28 baselines screened: none showed a replicable edge.
- Components tested (RSI, Aroon, Bears-Bulls-Impulse, atr_expansion, Chandelier Exit): none improved entries or results.
  One near-pass (Bears-Bulls-Impulse) did not replicate on EMA/SMA.
- Entry study across all 28 baselines: no baseline cross carries replicable forward information.
- Signal types (breakouts 20/55, momentum 20/60/120): none passes the rule on both samples. Momentum 20 looked good
  out-of-sample only (gain about 0.01 ATR net of spread), and it did not appear in-sample. Breakouts tend to
  *reverse* over 20 to 40 bars; that is a hypothesis, not a finding.
- The whole NNFX base system earns about 0.05R per trade out-of-sample. That is inside the noise of about ±0.04R
  at roughly 900 trades.
- Holdout on 2006-2015 (never seen before): momentum 20, the breakout fades (20 and 55) and the Kijun base system all
  failed their preset rules (the base system: -0.03R out-of-sample, beats 14% of random runs; the tuned period was 19
  instead of 171).
- Batch screen on 2016-2025 with the correction: 11 of 28 baselines beat 90% of their random runs (about 3 expected by
  luck); none survives the correction (best adj.beats 79%).

**Conclusion so far:** price-based daily entries on these 20 pairs over 2016 to 2025 carry essentially no replicable
forward information. The engine is trustworthy; the strategy has not yet earned any trust.

### 2.6 What would change the picture
- More independent data: 2006 to 2015 for the same pairs, then more pairs. Any hypothesis is registered first and
  run once.
- A different edge source: a mean-reversion (fade) test of breakouts, volume data for volume filters, other
  timeframes.
- Spreads that depend on time of day, and a real broker swap markup (swap itself is built: `--rates`, see CLAUDE.md).
- Final steps for anything that survives: a demo forward test, then a one-time final holdout.

---

## 3. Phase one: find a baseline + C1 pair

**Question:** is there any baseline + first confirmation (C1) pair whose trades beat luck, on data it was not chosen on?
**Out of scope until a pair survives:** C2, volume/filter and the exit indicator (phase two), position limits, swap.
**Success:** at least one pair passes every stage below. **Failure is a result too:** if nothing passes, phase one ends
with "no pair", and what to do next is decided then, not by loosening the rules.

### 3.1 Things fixed before any run (written down, never changed after seeing results)
- **Framework:** the NNFX rules in section 1, spreads from `costs.csv`, no swap.
- **Baseline parameters:** ONE conventional value per baseline (the indicator's own default, e.g. Kijun 26), listed in
  a table before the first run. No searching. A robustness check on nearby values happens only for finalists (stage 3).
- **The C1 pool:** the state components that can confirm: today 8, `rsi`, `aroon`, `bears_bulls_impulse`, `adx_di`, `cmo`,
  `disparity_index`, `pmo` and `tii` (the last five ported from the MT4 folder on 2026-11-17). Decide the whole pool now:
  every indicator added later raises the number of tries.
- **"Neutral":** the neutral baseline is Kijun(26), the standard NNFX line (we found no baseline whose crosses carry
  information, so any would do; it is fixed by convention, not picked from results). The neutral C1 is the random
  stand-in component (same state frequencies and persistence, no knowledge of prices), which the controls already build.
  A C1 is judged by how far it beats its random stand-ins, so no separate "neutral C1" run is needed.
- **Score of a pair:** the system's out-of-sample expectancy in R, in standard deviations of its own 200 random-direction
  runs (`--control`, same signals, stops, costs). Never raw expectancy: a pair with fewer trades is noisier, so the lucky
  small sample would win.
- **The tries ledger:** every pair tested is logged (nothing is dropped), in `results/` and in the ledger
  `.claude/results/tries_ledger.md`. M = the rows in the ledger (+ `--prior-tries`). It starts at 0: the logs of the
  earlier experiments were cleared at the user's request (their conclusions are in `.claude/memory/findings-so-far.md`).
  Caveat: the 2016-2025 data was looked at a lot before, so selection bias is still there, which is why confirmation on
  2006-2015 and the forward test carry the weight.

### 3.2 Data roles
| Data | Years | Role |
|---|---|---|
| `../data/post2016-in-sample`, `post2016-out-of-sample` | 2016-2025 | stage 1, finding candidates. Nothing is tuned, so both folders are fair tests. |
| `../data/pre2016-all` | 2006-2015 | stage 2, confirmation of finalists, once. (Touched earlier by four tests of other hypotheses, none a baseline + C1 pair.) |
| new data | 2026 on, plus a demo forward test | stage 4, the final check. |

### 3.3 Stages and pass rules (preset)
1. **Selection (2016-2025).** Test every baseline x C1 pair as one batch (all pairs at once, no loop: 28 x 8 = 224 pairs at
   the moment, seconds to run; the loop "fix one, vary the other, repeat" is only worth it if the grid gets too big).
   A pair is a finalist if on the out-of-sample folder its expectancy is at least 0.03R, it beats at least 90% of its random
   runs AND still beats 90% after allowing for M tries (F(z)^M, see CLAUDE.md), and on the in-sample folder its
   expectancy is above 0 (a free replication on 10 other pairs).
2. **Confirmation (2006-2015).** Each finalist is run ONCE with the same parameters on all 20 pairs: expectancy at least
   0.03R and beating at least 90% of random runs (with more than 2 finalists the bar is raised to allow for the number
   of finalists).
3. **Robustness.** On survivors only: spreads x2 still leave expectancy above 0; neighbouring parameter values (about
   +-20%) still beat random; positive in at least 60% of the years; no single pair gives more than half of the total R.
4. **Final check.** A demo forward test and new data as it arrives. Only then does a pair go on to phase two.

### 3.4 The tools (built) and what is still open
- **`--grid <plan file>`** runs every baseline x C1 pair of the plan in one batch, scores each, applies the tries
  correction, runs only the finalists on the confirmation data and logs every pair (`grid_log.csv`) and one ledger row. See
  CLAUDE.md for the command and `phase1/plan.txt` for the plan.
- **`phase1/plan.txt`** holds the 28-row parameter table and the 8-indicator C1 pool. It is a DRAFT: each value says where it
  comes from (`[mt4]`, `[class]`, `[conv]`, `[guess]`). It must be approved, then it is never changed.
- **Still to build (stage 3):** robustness breakdowns for the survivors (by year, by pair, spreads x2, nearby parameters).

## 4. Adding your own indicator (AI-friendly)

Baselines and components are single self-contained headers; no other file needs editing.
- Baseline: copy `templates/baseline_template.hpp` to `include/baselines/<name>.hpp`.
- Confirmation, exit or filter: copy `templates/component_template.hpp` to `include/components/<name>.hpp`.
- Build and run `./build/engine_tests`. The generic tests check the contract, and a hand-worked-numbers test is
  optional for non-obvious indicators.

## 5. Typical commands

```
# one system: Kijun(26) + Aroon as the first confirmation; metrics, per-asset table and R-multiples for both data sets
./build/backtester --insample ../data/post2016-in-sample --outsample ../data/post2016-out-of-sample \
    --costs costs.csv --baseline kijun --params period=26 --confirm aroon

# does the confirmation add anything over random stand-ins at the entry level? (logged, with the rule in the note)
./build/backtester --insample ../data/post2016-in-sample --outsample ../data/post2016-out-of-sample --costs costs.csv \
    --baseline kijun --params period=26 --confirm aroon --entry-study 1000 --entry-control 200 \
    --log-dir results --note "rule: ..."

# signal study without a baseline
./build/backtester --signal momentum:20 --entry-study 1000 \
    --insample ../data/post2016-in-sample --outsample ../data/post2016-out-of-sample --costs costs.csv
```

Read `--help` for every option. All logs are in `results/` (ignored by git, as are the baselines and components).
