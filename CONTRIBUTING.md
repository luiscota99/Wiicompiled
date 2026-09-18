# Contributing to WiiCompiled

Thanks for wanting to help! A few ground rules

## The short version

- Code is judged on quality, not where it came from
- You must be able understand and be able to explain every line you submit.
- PR descriptions and responses must be written by you, **not** generated.
- Accuracy is the bar for anything touching game behavior.

## Code quality

We don't care how your code came into existence. What we care about is whether it meets or improves the
project's patterns and standards, and the only way we measure that is by
reading it.

Low-quality code won't be merged, regardless of origin. AI slop and human slop
get the same treatment.

## If you use AI tools

That's ok, but rules apply:

1. **You must be able to explain your changes.** If a reviewer asks why a line
   exists or what a function does and you can't answer, the PR will be closed.
   "The AI wrote it" is not an explanation.
2. **Write your own PR description.** The description exists so reviewers know
   what you changed and why, in your words. Generated descriptions tend to
   describe everything and explain nothing, and they will get your PR closed.

## Pull requests

- Keep PRs focused. try and keep it at 1 change per PR. 
  Small PRs get reviewed fast.
- Explain **what** and **why**. Reference the issue if there is one.
- For anything affecting game behavior: identical behavior to real hardware is
  the goal. Be prepared to show your change doesn't diverge from the original
  game (hardware comparison, logs, whatever fits).
- Review feedback. It's about the code, not about you ;).

## Bug reports

See the FAQ in the [README](README.md)

## A note on related projects

WiiCompiled, Wheel Wizard, and other projects in this ecosystem are developed
independently and each has its **own** contribution rules and all have their own
rules around AI usage. What applies here does not automatically apply there,
and vice versa. Check each project's own CONTRIBUTING file.

## Legal

- Never!!! include Nintendo code, assets, or game data in a PR, an issue, or
  anywhere else in this project. No exceptions.
- By contributing, you agree your contributions are licensed under
  [GPL v3.0](LICENSE), like the rest of the project.

---

## The FFCC port branch

Everything above applies. The following is specific to the Final Fantasy Crystal Chronicles port on the
`ffcc-port` branch.

### Ground rules

- **No game data, ever.** No DOL, disc files, saves, symbol dumps that contain code or data bytes,
  translated output (`generated/`), or link recordings (they carry streamed game data). The
  `.gitignore` blocks the usual suspects; check `git status` before you commit anyway.
- **Evidence over inference.** A change to game-facing behaviour cites where the behaviour comes
  from: the FFCC decompilation (file and line), a symbol in the map, a recorded link trace, or a
  measurement in the runtime log. "It seemed to work" is a test note, not a justification.
- **Keep the Mario Kart build intact.** Anything game-specific goes in `projects/ffcc/`, in
  `runtime/src/hle/ffcc/`, or behind `RECOMP_PROJECT_FFCC` in shared files. If you touch a shared
  file, say in the PR whether you built the upstream target too.
- **Disclose AI assistance.** If a model wrote or shaped the change, say so in the PR and keep the
  `Co-Authored-By` trailer on the commits; also say how you verified the result yourself.

### Before opening a pull request

1. **Clean clone builds.** Follow the README quick start in a fresh clone (not your working tree)
   and boot the game. Most first-time failures live in steps the working tree has absorbed.
2. **Diagnostics out.** Temporary logs, counters and test modes are fine while you work; strip them
   before the PR or gate them behind an environment variable that is documented in `docs/ffcc.md`.
3. **Say how you tested.** Which scene, how many players, which controllers, and for anything near
   the handheld link, attach a trace recorded with `WIICOMPILED_GBA_TRACE=<file>` from a run that
   shows the fix (the replay harness in `scripts/ffcc/` can replay it against a fresh client).
4. **Small and separable.** One concern per PR. A rendering fix, a link change and a doc edit are
   three PRs, each reviewable in one sitting.
5. **Commit messages** explain the why and the evidence, not just the what. Reference decomp lines
   as `joybus.cpp:1234` and runtime measurements in plain words.

### Reporting a bug

Open an issue with: the commit you built, your OS and GPU, the number of players and controllers,
what you did, what happened, and the runtime log (`build-ffcc/UserData/Logs` or the console output).
For link problems add a `WIICOMPILED_GBA_TRACE` recording of the failing session. For rendering
problems a Dolphin capture of the same scene is the single most useful thing you can attach.

### Where things are

- `projects/ffcc/recomp.yml` - project manifest (memory layout, entry points, hooks, DOL hash).
- `runtime/src/hle/project_guest_addresses.h` - per-project SDK addresses.
- `runtime/src/hle/ffcc/` - FFCC-only runtime modules; the handheld link is `ffcc_gba*.cpp`.
- `scripts/ffcc/` - mGBA build script, link replay harness and client probe.
- `docs/ffcc.md` - the full guide, environment variables, and how to adapt the runtime to another game.
