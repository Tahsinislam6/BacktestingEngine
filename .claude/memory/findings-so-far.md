# Findings before the fresh start (2026-11-17). Raw logs deleted; these are the conclusions

All on daily FX, 20 pairs, spreads from costs.csv, NNFX stop/target/runner rules.
- 28 baselines screened (tuned in-sample, tested on 10 other pairs, vs 200 random-direction runs): none had a replicable
  edge. 11 of 28 beat 90% of their own random runs (about 3 expected by luck); with the correction for 28 tries none passed
  (best adjusted 79%).
- Kijun base system, 2016-2025 out-of-sample: about +0.055R per trade (901 trades), inside the +-0.04R noise. On 2006-2015
  (never seen): -0.03R, beat only 14% of random directions, and the tuned period was 19 instead of 171 (unstable).
- Components tried on Kijun (RSI, Aroon, Bears-Bulls-Impulse, atr_expansion as filter, Chandelier Exit as exit): none improved
  entries versus random stand-ins. Bears-Bulls-Impulse was a near-pass on Kijun and did not replicate on EMA and SMA.
- Entry study (what price does 1-40 bars after a signal, in ATR, vs coin-flip directions): none of the 28 baselines carried
  replicable forward information.
- Other signals, 2016-2025: breakouts (20, 55) and momentum (20, 60, 120) failed the rule on both folders. Momentum 20 looked
  good out-of-sample only (about 0.03-0.05 ATR gross, 0.01 net). Breakouts tended to reverse over 20-40 bars.
- Holdout 2006-2015, four preregistered tests, all failed: momentum 20; breakout 20 fade; breakout 55 fade (one folder
  -0.289 ATR, beats 7%, the other folder flat); the Kijun base system.
- Useful facts: noise floor about +-0.04R at 900 trades; with a 1 ATR target and 1.5 ATR stop random directions win about 60%
  of races, so the target half breaks even by construction; the first tuned-then-tested results were mostly noise.
- Overall: price-based daily entries on these pairs showed no replicable forward information. The engine is trusted; no
  strategy has earned trust yet.

# Phase one grid, run once on 2026-11-17 (plan phase1/plan.txt, 28 baselines x 8 C1 = 224 pairs, prior-tries 0)
Pass rule (in the log note and the ledger): finalist = OOS expectancy >= 0.03R, beats >= 90% of own 200 random runs, adjusted beats
(M = 224) >= 90%, in-sample expectancy > 0; candidate = finalist that also passes once on 2006-2015.
RESULT: 0 finalists, 0 candidates. The holdout (pre2016-all) was NOT touched, so it is still unused for phase one.
- Mean OOS expectancy -0.022R, mean in-sample -0.036R (the average pair loses about the spread). Only 2 of 224 pairs reached +0.03R OOS
  and only 3 were positive on both folders. 22 of 224 (10%) beat 90% of their own random runs: exactly what luck predicts (about 22).
  Best adjusted beats 36% (ptl_fast + bears_bulls_impulse: +0.035R OOS, beats 100%, but -0.018R in-sample).
- By C1 (mean OOS R): bears_bulls_impulse -0.006, disparity_index -0.009, cmo -0.012, aroon -0.016, adx_di -0.018, rsi -0.026, tii -0.033,
  pmo -0.057. By baseline the best were ptl_fast +0.006 and ssl +0.003; the worst alma, vidya, gd (about -0.04).
- Conclusion: no baseline + C1 pair shows anything beyond luck on 2016-2025. Do not loosen the rules or re-run the same plan.
Raw rows: results/grid_log.csv (git-ignored); the ledger row is in results/tries_ledger.md.

Note: the phase-one grid above was run with spreads only. Sanity run after swap was added (kijun 26 + aroon, in-sample, not a counted
try): total return -66.6% without swap vs -66.4% with it; trade count unchanged (swap changes P&L, not signals). Per pair: USDJPY longs
+1,310 swap, AUDNZD -356: carry is visible per pair but small overall for this system.

# Phase one grid RE-RUN WITH SWAP, 2026-11-17 (same frozen plan, rates ../data/rates.csv, markup 0, no SGD rate; prior-tries 224 so M = 448)
Same pass rule as the first run (adjusted beats now with M = 448). RESULT: 0 finalists, 0 candidates, holdout untouched again.
- Swap barely changes anything: mean OOS expectancy -0.0219R without swap vs -0.0209R with it (per-pair change between -0.003R and
  +0.005R, mean +0.001R; every C1 gains about 0.0004 to 0.0018R). Pairs with OOS >= +0.03R: 2 -> 1. Pairs beating 90% of random: 22 -> 24
  (luck predicts about 22). Pairs positive on both folders: 3 -> 2. Best pair ptl_fast + bears_bulls_impulse: +0.036R OOS, in-sample
  -0.019R, adjusted beats 12% (needs 90%).
- Total tries now 448 on the 2016-2025 data (two grid runs of the same 224 pairs). Do not run this plan again.

# Phase one grid PLAN B, 2026-11-17 (phase1/plan_13c1.txt, 28 baselines x 13 C1 = 364 pairs, swap ON, markup 0, M = 364, prior-tries 0)
Same pass rule (adjusted beats with M = 364). RESULT: 0 finalists, 0 candidates; holdout (pre2016-all) still untouched.
- Mean OOS expectancy -0.0229R (IS -0.0347R). OOS >= +0.03R: 4 pairs; beating 90% of random: 37 (luck predicts about 36); positive on both
  folders: 4; best adjusted beats 18% (ptl_fast + bears_bulls_impulse again: +0.036R OOS, -0.019R IS).
- New C1s (mean OOS R): lwpi +0.0022 (the only C1 above 0; 12 of its 28 pairs beat 90% of random but its in-sample is negative), hlc_trend -0.021,
  ttf -0.035, laguerre -0.035, andean_oscillator -0.041. Old C1s as before (pmo worst at -0.055). New five as a group: 9% of pairs beat 90% of random,
  the old eight 11%: both at the luck rate.
- lwpi is a LEAD, not a finding: no finalist, in-sample negative. Testing it further would be a new, separately pre-registered hypothesis (and its
  contrarian reading is a different candidate); the untouched 2006-2015 holdout is the only data that could confirm it.
- Total grid runs so far: 3 (224 + 224 + 364 pair-runs) on 2016-2025. Do not re-run plan B or the frozen plan.

# Plan C: the LWPI lead on 2006-2015, 2026-11-17 (phase1/plan_lwpi.txt, 28 baselines x lwpi, M = 28, swap ON, data pre2016-in/out-of-sample)
Pass rules written first (plan header and --note): rule 1 grid finalists (M = 28); rule 2 the lead replicates only if (a) mean OOS expectancy > 0,
(b) mean IS expectancy > 0 and (c) at least 8 of 28 pairs beat 90% of random OOS. RESULT: the lead did NOT replicate. 0 finalists.
- (a) mean OOS -0.0041R fail; (b) mean IS -0.0013R fail; (c) 2 of 28 pairs beat 90% of random (luck ~2.8) fail. Best pair +0.033R OOS, adjusted 4%.
- Per-baseline OOS expectancy 2006-2015 vs 2016-2025 correlates at 0.06: the 2016-2025 pattern (12 of 28 pairs beating 90%, mean +0.0022R) was
  selection luck, not a property of lwpi.
- The 2006-2015 data is now used up for baseline + C1 tests (previously only for four other hypotheses). Nothing is left unseen except future data.
- Conclusion so far: 4 grid runs (224 + 224 + 364 + 28 pairs) and no pair with an edge. lwpi is closed as a lead.

# Phase one grid PLAN D with filters, 2026-11-17 (phase1/plan_filters.txt, 28 baselines x 13 C1 x 5 filters = 1,820 pairs, swap ON, prior-tries 364, M = 2,184)
Pass rule as every grid (adjusted beats with M = 2,184), no holdout (2006-2015 used up). RESULT: 0 finalists. Max adjusted beats 0.1% (the bar at this M needs a very high z).
- Mean OOS expectancy -0.0312R (IS -0.0393R): filters made the average pair slightly WORSE than unfiltered (-0.0229R), and cut trades from about 3,400 to 1,527.
- Pairs beating 90% of their own random runs: 105 of 1,820 (5.8%), BELOW the 10% luck rate. OOS >= +0.03R: 110; positive on both folders: 56; positive on both and beating
  90%: 13 (best look: ssl + disparity_index + volatility_ratio, OOS +0.061R, IS +0.102R, beats 97%, 437 trades; third_gen_ma + hlc_trend + atr_expansion OOS +0.137R, 403 trades).
  With 1,820 pairs such pairs are expected by luck.
- By filter (mean OOS R / share of pairs beating 90% of random): atr_expansion -0.007 / 11% (the only one at the luck rate), normalized_volume -0.027 / 9%, volatility_ratio -0.029 / 3%,
  ttms -0.045 / 3%, adx_strength -0.049 / 3%. The real-volume filter did not stand out from the price-based ones.
- Conclusion: filters do not rescue any baseline + C1 pair. 5 grids so far (224 + 224 + 364 + 28 + 1,820 pair-runs) and no edge. Do not re-run plan D.


## No scaling out (2026-11-18, user's hypothesis that the target half hurts)
Built `--no-scaling`: one trade per signal with the whole 2% risk, stop 1.5 ATR, no take-profit, no breakeven, stop trails 1.5 ATR
behind the best close from the bar after entry (needed: with no target nothing else would ever close the trade). Plan B (364 pairs,
frozen phase1/plan_13c1.txt) run once with swap, prior-tries 2,184 (M = 2,548): 0 finalists. Mean OOS expectancy -0.0525R (IS -0.0619R)
vs -0.0229R / -0.0347R with scaling; the mean "beats" is 36% (below the 50% of a coin flip); best row ptl_fast + bears_bulls_impulse
OOS +0.001R. So scaling out was NOT what hurt: the trail-from-entry single trade is worse here. Trades per run halve (about 1,530 vs 2,900).

## The user's trailing-stop rule (2026-11-18): `--trail-after 2`
Rule: one trade, whole 2% risk, stop 1.5 ATR, no target/breakeven; the stop stays until a CLOSE is 2 x entry-ATR in profit, then trails 1.5 x entry-ATR behind
the best close. Plan B (364 pairs) run once, swap on, prior-tries 2,548 (M = 2,912): 0 finalists. Mean OOS -0.0671R (IS -0.0709R), mean beats 36%,
8/364 pairs beat 90% (luck would give ~36), 21 positive OOS. Best OOS rows +0.06R with IS negative. Summary of the three exits on plan B:
scaled -0.023R / no-scaling trail-from-start -0.053R / trail-after-2 -0.067R. The exit rule does not create an edge. Pasted tables from the user
(ATR/trade scenarios with +2.5 ATR) were simulations whose 'mixed' row does not match the blend of its own rows; not evidence.

## Parameter sweep, plan E (2026-11-18): `phase1/plan_sweep.txt`, FROZEN
130 baseline variants (28 baselines x up to 5 values of the first parameter, x0.5..x2 snapped to the legal grid) x 39 C1 variants (13 C1s x 3 values of the
first parameter) = 5,070 pairs, default exits, swap on, prior-tries 2,912 (M = 7,982): 0 finalists, 0 candidates. Mean OOS -0.0261R (IS -0.0351R, same as with
default parameters), mean beats 46.5%, 524 pairs beat 90% (luck predicts ~507), 868 positive OOS, 26 pass the raw bars (OOS >= 0.03R, beats >= 90%, IS > 0)
but none survive the correction. Biggest OOS rows all use hlc_trend(close_period=10) (e.g. trimagen period=40: IS +0.076R, OOS +0.147R, 207 trades;
ssl 20, sinewma 40, coral 51, ema 30 similar) - few trades, one shared slow C1, so one lucky draw seen five times, not five confirmations. Only new data can test it.
Plan files may now repeat a baseline with other parameters and give a C1 parameters (`c1 rsi period=7`; the grid log names it `rsi(period=7)`).

## Plan F: the hlc_trend(close_period=10) lead on 2006-2015 (2026-11-18): did NOT replicate. `phase1/plan_hlc.txt` FROZEN
33 pairs (plan B's 28 baselines + the five lead variants) x hlc_trend(10), M = 33, 2006-2015, swap on. Rule 2 over the 28 baselines: (a) mean OOS -0.0106R FAIL,
(b) mean IS +0.0249R pass, (c) 2 of 28 beat 90% (needed 8) FAIL; 0 finalists (best adjusted beats 48%). The five exact pairs: ema 30 OOS +0.112R (beats 94%),
sinewma 40 +0.100R (89.5%), trimagen 40 +0.056R (70%), coral 51 +0.041R (44%), ssl 20 -0.110R (10%); mean beats of the 28 = 59%. Mixed, mildly positive in-sample, not
a pass. No 2006-2015 data is left that was not used for some lead; only new data (2026 on) can say more.

## The user's "plain" strategy (2026-11-18): stop 2.5 ATR(20), no target, no trail, close on the opposite signal, risk 0.25%
Flags: `--no-scaling --no-trail --stop-atr 2.5 --atr-period 20 --risk 0.0025 --cross-exit` (grid). Entries = baseline cross + C1 (user's choice), don't-chase 1 ATR.
Control = random directions with the same timing, a random trade closes at the NEXT cross either way (strategy + control + grid now allow the cross-back exit;
the grid stays WITHOUT it unless `--cross-exit`). Plan B (364 pairs), swap on, prior-tries 7,982 (M = 8,346): 0 finalists. Mean OOS -0.0369R (IS -0.0358R),
~2,320 trades per pair, only 1 of 364 positive OOS, 0 beat 90% (luck ~36) and the MEAN beats is 9.9%: the real direction does WORSE than random directions in
almost every pair (best pair ptl_fast + bears_bulls_impulse: -0.007R, beats 80%). Same direction as the earlier note that breakout directions reverse: after a baseline
cross price tends to give back the move on this data. 364 pairs share one market history, so they are not independent evidence.

## C1 exit switch (built 2026-11-18, not yet run for real)
`--c1-exit` (grid): GridOptions.c1Exit puts each pair's C1 in the exit-indicator slot, so a C1 state against the trade closes both halves (ExitReason::Indicator). In the random-direction control the exit reads the state against the baseline signal's direction (signalSign_), else a random trade against the C1 would close next bar. Earlier grids never used a C1 exit (entry confirmation only). 1,282 tests pass; 9 mutants, 8 caught, 1 survivor is the main.cpp wiring (checked end to end on kijun+rsi in the scratchpad: -0.027R without, -0.023R with, not logged). A real run needs a pass rule in --note first and counts as new tries.

## C1-exit runs (plan B, 364 pairs each, 2026-11-18, swap on, M=8,710 and 9,074)
Plain strategy + --c1-exit (--cross-exit too): 0 finalists. Mean OOS -0.0314R (IS -0.0315R), 2/364 positive OOS, 0 pairs beat 90%, mean beats 12.2%, ~2,324 trades/pair. Best ssl + hlc_trend +0.002R (beats 71%). Without the C1 exit it was -0.0369R, so the C1 exit helped slightly but stayed negative.
Default exits + --c1-exit (no cross-back exit): 0 finalists. Mean OOS -0.0307R (IS -0.0358R), 22/364 positive, 8 pairs beat 90% (luck predicts ~36 at 10%), mean beats 31.8%, ~3,839 trades/pair. Best third_gen_ma + hlc_trend +0.057R (beats 92%) but fails the correction. Original default plan B was about -0.023R, so the C1 exit made it slightly worse.

## Open search over exits/entries (user asked "find an edge", chose open search; 42 configs x plan B = 15,288 pairs, 2026-11-18)
Configs: stop 1.5/2.5 ATR x {scaled, no-scaling trail, trail-after 2, plain no-trail, scaled no-trail} x {no extra exit, cross-exit, c1-exit, both}, plus entry variants (confirm-window 1/3/15, one-candle, continuation) on scaled + cross-exit. Priors 9,074 -> M up to ~24,400. Script in scratchpad run_search.sh; every config logged + ledgered.
Result: 0 finalists, 0 candidates. Best config mean OOS -0.014R (scaled stop 2.5, no cross/C1 exit); almost all configs -0.02 to -0.06R. Configs with NO exit at all (plain/scaled no-trail with no cross or C1 exit: cfg 1, 2, 21, 22) are degenerate (a trade never closes except at its stop, 100-400 trades, mean -0.5 to -1.0R); ignore them.
Only 2 of 15,288 pairs had OOS>=0.03R, IS>0 and beats>=90%: ptl_fast length=3 + lwpi (scaled stop 2.5: IS +0.003, OOS +0.033) and gchannel period=100 + aroon (trail-after 2 stop 2.5: IS +0.004, OOS +0.058). Both fail the correction (adj beats ~0); at this many tries several such pairs are expected by luck. Leads at most, not edges.

## Plan G: the 55-day breakout (turtle_channel 55/55 pure breakout and 55/20 Turtle S2) x 13 C1s x 41 strategy configs (2026-11-18)
Plan phase1/plan_breakout55.txt (FROZEN). 41 configs (stop 1.5/2.5 x scaled / no-scaling trail / trail-after 2 / plain / scaled-no-trail x cross-exit / c1-exit / both, plus confirm-window 1/3/15, one-candle, continuation) x 26 pairs = 1,066 pairs, all with --no-chase-limit (the 1 ATR chase check skips most breakouts: the line is the opposite 55-bar extreme). Needed two new flags: --max-distance <x>, --no-chase-limit; turtle_channel stop range widened from 5-30 to 5-60. Priors 24,362 -> M up to ~25,400.
Result: 0 finalists, 0 candidates. 55/55 mean OOS -0.0445R (IS -0.134R), ~435 trades/pair, 16 pairs beat 90%; 55/20 mean OOS -0.0274R (IS -0.073R), ~1,359 trades/pair. Best config x baseline: 55/55 trail-after 2 stop 2.5 +0.025R OOS (13/13 C1s positive OOS but IS -0.044R). Best pair: 55/55 + tii, no-scaling trail stop 1.5, OOS +0.096R (beats 96%) but IS -0.286R. No pair had OOS>=0.03R, IS>0 and beats>=90%. IS is consistently worse than OOS for 55/55, so the OOS positives are not consistent across the two asset sets.

## Plan H: the 55-day breakout with NO C1 (c1 none), 18 configs x 2 baselines = 36 pairs (2026-11-18)
User asked "no test with no c1". Added `c1 none` to the grid plan format (no confirmation; explicit exception to never-a-baseline-alone; shows as "none" in table/log). Plan phase1/plan_breakout55_noc1.txt (FROZEN). Configs: stop 1.5/2.5 x {scaled, no-scaling trail, trail-after 2} (+ cross-exit; plain and scaled no-trail only with cross-exit), plus one-candle and continuation; --no-chase-limit throughout. Priors 25,428 -> 25,462.
Result: 0 finalists, 0 candidates. 55/55: mean OOS -0.058R (IS -0.138R), ~570-600 trades/pair, 0 beat 90%. 55/20: mean OOS -0.027R (IS -0.075R), ~1,975 trades. Best pair 55/55 trail-after 2 stop 1.5 + cross-exit: OOS +0.025R, IS -0.009R, beats 63%. No pair had OOS>=0.03R + IS>0 + beats>=90%. Removing the C1 gave about the same picture as plan G, so the C1 was neither helping nor hurting the breakout.
