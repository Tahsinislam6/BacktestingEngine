# Decisions (by the user)

- (Superseded 2026-11-17: swap was added, see below.) No swap/financing for now. Spreads = Pepperstone spread-betting list in `costs.csv` (USDSGD 6.3 is an outlier). Fills at the
  daily close may face wider spreads: stress with `--spread-multiplier 1.5` to 2.
- `include/baselines/*` and `include/components/*` stay local (git-ignored). AI automation will add indicators later; keep the
  code free of bloat.
- Indicators come from `~/Downloads/Trading/MT4/Indicators` (MT4 .mq4 sources; ports copy the MT4 quirks and are tested with
  hand-worked numbers).
- 2026-11-17: solo baseline testing, flip mode, the parameter search, validation (time split, walk-forward), the old
  `--screen`/batch screen and `--mode` were REMOVED. A run is baseline + `--params` + at least one component.
  (Old code is in git history and in a scratch backup; the batch screen with the tries correction was never committed.)
- 2026-11-17: fresh start. All csv logs from the earlier experiments were deleted at the user's request and the tries count
  starts at 0 for phase one. Caveat kept on record: the 2016-2025 data was looked at many times before, so selection bias is
  still there; confirmation on 2006-2015 and a forward test matter more because of it.
- The user prefers one batch over a loop (fix one, vary the other, repeat) because every loop round adds tries; the batch of
  every baseline x C1 pair is the plan (phase one).
- 2026-11-17: five MT4 indicators were ported as C1 candidates (adx_di, cmo, disparity_index, pmo, tii), tests first with
  hand-worked numbers and a mutation check (3 surviving ADX mutants are equivalent: each clamp of +DM/-DM is redundant
  given the comparison that follows). Not ported: indicators that exist only as .ex4 (no source), and the rest of the MT4
  folder (Laguerre, QQE, TTF, Decycler, Cyber-Cycle, Andean, LSMA_RVI, LWPI, WPR_ma, Trigger-Lines, kase stochastic,
  JpOscillator, ...) - say which to add; each one raises the number of tries.
- 2026-11-17: `--grid` built (see phase-one.md). Smoke-tested only on a throwaway 2x2 plan (kijun/ema x rsi/aroon, logs in the
  scratchpad, not a counted try): 4 pairs in 0.8 s, so the full 224 pairs take about 45 s. The full grid has NOT been run
  and its results have not been looked at: the plan is awaiting approval.
- 2026-11-17: the user approved phase1/plan.txt (including the two [guess] rows: kalman_filter, vidya) and the pass rule, and asked for the
  grid to be run. It was run once (31 s): 0 finalists. phase1/plan.txt is now frozen: any new indicator or parameter set is a NEW
  plan, with the 224 pairs counted in --prior-tries.
- 2026-11-17: SWAP ADDED at the user's request ("get it from dukascopy"). Dukascopy has no swap history, so with the user's choice
  the history is central-bank/interbank rates from FRED/OECD (`../data/make_rates.py` -> `../data/rates.csv`), swap = rate
  differential per night held, markup default 0 (`--swap-markup`). Off unless `--rates` is given. Singapore has no series (USDSGD gets
  no interest), NZD is spliced, monthly resolution. The phase-one grid (224 pairs) had been run WITHOUT swap; a rerun with swap is a
  new test whose 224 pairs count in --prior-tries and needs its own pass rule written first.
- 2026-11-17: the phase-one grid was re-run with swap at the user's request: 0 finalists (see findings-so-far.md). The plan stays frozen.
- 2026-11-17: components now live one per folder WITH their own tests, all git-ignored: include/components/<name>/<name>.hpp and
  <name>_test.cpp (CMake globs both). Five more MT4 indicators were ported as state components: laguerre (Laguerre RSI vs 0.5),
  lwpi (momentum reading: below 50 = bars close above their open = bullish; a contrarian reading is possible and is a different
  candidate), hlc_trend (first/second line rule), ttf (Trend Trigger Factor, T3-smoothed, sign), andean_oscillator (bull vs bear).
  Left out on purpose: Cyber Cycle and Decycler Oscillator (a straight-line trend decays to 0 in both, so they cannot tell a steady
  climb from a steady fall). Pool of State components is now 14 (13 usable as C1 plus chandelier_exit as an exit). The frozen
  phase-one plan still lists only its 8 C1s; a plan with the new ones is a NEW plan (new tries: counted with --prior-tries 448).
- 2026-11-17: on the user's request plan B (phase1/plan_13c1.txt, 13 C1s, 364 pairs) was written as a DRAFT. Open decision for the user:
  M = 364 with --prior-tries 0 (my recommendation: distinct hypotheses) or --prior-tries 448 (stricter). This supersedes the earlier
  note that a new plan must use --prior-tries 448: that was for a plan of NEW pairs only.
- 2026-11-17: the user approved plan B with M = 364 and --prior-tries 0; it was run once (61 s): 0 finalists. The open question about counting is closed.
- 2026-11-17: the user asked to test the lwpi lead on the holdout. I pre-registered plan C (grid rule M = 28 plus a three-part replication rule) and ran it once on
  2006-2015: it did not replicate. The rule was written by me without a separate approval step, on the user's request to test it.
- 2026-11-17: VOLUME added at the user's request ("let's add volume"). Engine: Bar::volume (loader by header name, Sunday merge sums,
  aligned data carries it). Four GATE components ported from the MT4 Volume folder, each with its own tests: normalized_volume (real
  volume vs its average, level 100), volatility_ratio (SD ratio > 1), ttms (allows when NOT in the Bollinger-in-Keltner squeeze), adx_strength
  (ADX line > level; the level 25 is a convention, not in the file). Not ported: Silence (time-dependent static state), Stiffness (large),
  TDF, Vix-Fix (not a gate); Choppiness/WAE/Chaikin volatility exist only as .ex4. There is no grid dimension for filters yet.
- 2026-11-17: the user asked why the phase-one baselines and C1s were not tested with the filters and said go ahead: the grid got a filter dimension and plan D
  was drafted (not yet approved/run). My earlier idea of testing filters on one neutral system with random gates was dropped in favour of this.
- 2026-11-17: the user approved plan D with --prior-tries 364; it was run once (4 min 13 s): 0 finalists.
