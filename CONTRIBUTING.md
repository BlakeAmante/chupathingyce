# Contributing to ChupathingyCE

ChupathingyCE is a port of the Halo: Combat Evolved decompilation, built for stability and community games. It has its own code and releases, and takes changes through pull requests here.

## Pull requests

- **Open them against `ChupathingyCE/chupathingyce`'s `main`.**
- One change per pull request, with how you tested it (which platforms you built and ran).
- The shared game protocol stays as it is, so cross-play keeps working: don't change how games are joined or what's sent on the wire. New networked features go on Delta, ChupathingyCE's own channel, falling back silently when the other side doesn't speak it.
- The 32-bit builds must stay byte-identical unless the change is meant for them: `tools/port_neutrality_check.py` checks it.
- Commit messages, pull requests and issues are public, and they're posted to the ChupathingyCE Discord.

## Versions

- **`0.MINOR.0`** is a major release, with a theme.
- **`0.MINOR.PATCH`** is a regular release, with features and fixes.
- **A letter after it** is a hotfix of that release: crash, security or network fixes only, no features. Letters start at `a`, in order: 0.7.5, then 0.7.5a, 0.7.5b.
- **1.0.0** comes once the game is feature-complete and stable on every platform.

Test builds are the `nightly` release and `-testing` tags, outside the numbering. The network version is separate from the release version: a hotfix changes it only when cross-play needs it.

## Bugs

Use [the bug report form](https://github.com/ChupathingyCE/chupathingyce/issues/new?template=bug.yml), or #bug-reports on the [Discord](https://discord.gg/4BUm2FwuCB).

## Game files

ChupathingyCE never provides game files. Don't commit, attach or link maps, disc images or other copyrighted game data.
