# Goal and rules

Goal: build, with as little self-deception as possible, an NNFX (No Nonsense Forex) daily-FX algorithm, using only
components (baseline, C1, C2, volume/filter, exit) that survive honest testing. A negative result is a valid result.

Terms: C1 and C2 are the FIRST and SECOND CONFIRMATION. Volume/filter and the exit indicator are separate roles.

Rules (never broken)
- Nothing is searched or tuned. Baseline parameters are fixed in advance (`--params`); component parameters are defaults.
- A baseline is never tested alone: every run is baseline + at least one component.
- Write the pass rule in `--note` BEFORE running. One logged run per question. Never change a rule after seeing a result.
- Judge on data the choice was not made on. Compare with random stand-ins, never raw profit.
- Count every try (tries ledger). With M tries the chance one beats 90% of its own random runs by luck is 1 - 0.9^M.
- Tests first, then a mutation check (break the code on purpose and see a test fail), then the implementation is trusted.
