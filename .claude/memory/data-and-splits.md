# Data

Files live OUTSIDE the project, in `../data/` (next to the project), daily bars with millisecond timestamps.
- `in-sample`, `out-of-sample`: 10 pairs each, 2006-2025 (the names only mean "which 10 pairs"; nothing is tuned now).
- `post2016-in-sample`, `post2016-out-of-sample`: the same 20 pairs, 2016-2025. Used heavily before: fine for finding
  candidates, not for confirming them.
- `pre2016-in-sample`, `pre2016-out-of-sample`, `pre2016-all` (20 pairs): 2006-2015. Looked at only by four holdout tests of
  other hypotheses (momentum 20, breakout 20/55 fades, Kijun base system). The confirmation set for phase one.
- Still needed: new data (2026 on) and a demo forward test for the final check.
These folders were made with a throwaway script (split by timestamp at 2016-01-01 UTC); the originals are untouched.

Update 2026-11-17: 2006-2015 (pre2016-*) has now also been used for the LWPI replication (plan C, 28 pairs). It is no longer an untouched holdout for any
baseline + C1 hypothesis. Unseen data from now on can only be newly arriving data (2026 onward) or other pairs/markets.
