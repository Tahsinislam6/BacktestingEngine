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
