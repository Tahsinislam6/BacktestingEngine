# Phase one (one page; the full text is README.md section 3)

Question: does any baseline + C1 pair beat luck on data it was not chosen on?
Fixed before running: one conventional parameter per baseline (the indicator's own default), the whole C1 pool, framework rules.
Neutral baseline = Kijun(26); neutral C1 = the random stand-in component. Score = out-of-sample expectancy in standard
deviations of the system's own 200 random-direction runs.
Stage 1 (2016-2025, one batch of every baseline x C1 pair): out-of-sample expectancy >= 0.03R, beats >= 90% of random runs and
still >= 90% after the correction for M tries, in-sample expectancy > 0. Stage 2 (2006-2015, finalists once): expectancy >=
0.03R and beats >= 90%. Stage 3 (survivors): spreads x2, +-20% parameters, >= 60% of years positive, no single pair > half of
total R. Stage 4: demo forward test and new data.
Built 2026-11-17: `--grid <plan>` (CLAUDE.md has the command; src/grid.cpp, tests/grid_test.cpp, 1000+ tests) and a DRAFT plan
file `phase1/plan.txt` (28 baselines with documented parameter sources + the 8-indicator C1 pool). Still to build: stage-3
robustness breakdowns. RUN ONCE on 2026-11-17 (plan approved by the user): 0 finalists out of 224 pairs; holdout untouched. Details in findings-so-far.md. Stage 3 was not needed.
C1 pool as of 2026-11-17 (8): rsi, aroon, bears_bulls_impulse, adx_di (+DI vs -DI), cmo, disparity_index, pmo (sign of the PMO line, not its
signal cross: a steady linear climb has fading % momentum, so PMO-below-signal read bearish in an uptrend), tii (above/below 50).
28 baselines x 8 C1 = 224 pairs. Every later indicator raises M, so finish the pool before the first run.
Status: see `../results/tries_ledger.md` and git log.

Re-run WITH swap on 2026-11-17 (prior-tries 224): 0 finalists again. Phase one has now failed twice on 2016-2025 for the same 224 pairs;
the holdout 2006-2015 is still untouched for phase one.

Pool grew after the frozen plan: 13 C1 candidates exist now (the 8 of the plan + andean_oscillator, hlc_trend, laguerre, lwpi, ttf). Any grid that
uses them is a new plan file (not phase1/plan.txt) with --prior-tries 448 and its own pass rule.

Plan B written 2026-11-17: phase1/plan_13c1.txt = the same 28 baselines (same fixed parameters, checked by a test) x 13 C1s = 364 pairs
(the frozen plan's 8 + andean_oscillator, hlc_trend, laguerre, lwpi, ttf). STATUS: DRAFT, awaiting the user's approval; not run.
Counting proposed in the file: M = 364 distinct pairs, --prior-tries 0 (the 224 old pairs are inside it; the two earlier runs tested the
same hypotheses on the same data), with --prior-tries 448 as the stricter alternative. Run it with swap on (--rates ../data/rates.csv)
and the untouched holdout ../data/pre2016-all. The pass rule text to paste into --note is in the plan header.

PLAN B RUN 2026-11-17 (approved by the user, M = 364, prior-tries 0): 0 finalists again. Plan file phase1/plan_13c1.txt is now FROZEN. Details in findings-so-far.md.

PLAN C (lwpi lead on 2006-2015, M = 28): did NOT replicate (see findings-so-far.md). phase1/plan_lwpi.txt is FROZEN. The only unseen data left is the future:
new data as it arrives (2026 on) and a demo forward test.
