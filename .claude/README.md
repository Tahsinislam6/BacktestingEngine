# .claude: the project's memory

A hidden folder so that nobody (you, or a new Claude session) repeats work or forgets a decision.

- `memory/INDEX.md`  start here: one line per note.
- `memory/*.md`      what we decided, what we found, how the user likes to work.
- `results/`         the tries ledger and any result summaries worth keeping (raw csv logs go to `../results/`, which is
                     git-ignored and may be cleared; the ledger is what stays).

Rules for keeping it useful
1. At the start of a session read `memory/INDEX.md`, then the notes it points to that matter for the task.
2. After every logged test append one line to `results/tries_ledger.md` (what, data, pass rule, result). The number of
   rows in the ledger is M, the number of tries the multiple-testing correction must allow for.
3. After every decision or finding, add or update ONE note in `memory/` and its line in `INDEX.md`. Update an existing
   note instead of adding a duplicate; delete a note that turns out to be wrong.
4. Keep notes short and about things the code and git history do not already say.
