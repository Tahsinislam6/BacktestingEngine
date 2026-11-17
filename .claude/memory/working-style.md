# How the user likes to work

- C++ beginner; explain in plain language, say what a command does and what a result means.
- Tests first, then a mutation check, then implementation. Often answers "yes" / "go ahead" to a plan.
- Wants honest answers, including "that is probably a bad idea" and "the result is noise". Does not want results oversold.
- No code or folder bloat; AI automation will add indicators later, so adding one must stay a one-file job.
- Shell note for Claude: zsh does not split a quoted variable, so spell options out instead of using `$OPTS`.
- Mutation runs: rebuild with a bumped file time and watch for stale builds; a survivor must be re-run by hand before it is
  believed.
