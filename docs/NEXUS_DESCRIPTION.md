# Nexus page material

Nexus page (created by the author 2026-09-18): https://www.nexusmods.com/warcraft2/mods/8 (game domain `warcraft2`, mod id 8).

The Nexus texts live in `nexus/` as plain BBCode text files, ready to paste:

| File | Goes into |
|---|---|
| `nexus/NEXUS_SUMMARY.txt` | the short summary line (350 characters max) |
| `nexus/NEXUS_DESCRIPTION.txt` | the description editor in BBCode mode: installation and how to use `gameplay_options.toml` |
| `nexus/NEXUS_CHANGELOG.txt` | the changelog / release notes |

Keep them in step with `docs/USER_README.txt`, `docs/CONFIG_TUTORIAL.md` and `CHANGELOG.md` whenever a setting changes.

## State (2026-09-18)

- The GitHub repository is PUBLIC (the author's call, "so people can report issues"). License: MIT since
  1.0.10 (open source). The permission dropdowns on the Nexus page are the author's to set on the website; to agree
  with MIT they should allow uploads elsewhere, modification, conversion and asset use, each "with credit".
- `nexus/NEXUS_DESCRIPTION.txt` changed with 1.0.9 / 1.0.10 (GitHub, license and bug report paragraph): the description cannot be edited
  through the Nexus API, so it has to be pasted again on the website.
- On Nexus: **1.4.1** (file id 15, MAIN, 2026-09-19 10:49; 1.4.0 = file 14 and the author's 1.0.0 = file 13 are
  archived). Changelog entries exist for 1.0.0 to 1.4.1. The endpoint is append-only: before posting, read
  `GET /v1/games/warcraft2/mods/8/changelogs.json` and skip every version that already has an entry. One text file per
  version in `nexus/changelog_<ver>_api.txt`, one line per bullet. The page's version field lags the upload by a while.
- Every Nexus upload also gets a GitHub release: tag `v<version>` on the release commit, the same zip attached, notes =
  that version's CHANGELOG.md section (first one: v1.4.1).
- Future uploads still need the author's go-ahead for that version.
- Shipped config: examples as comments; only Heal, Slow, Bloodlust, Raise Dead and worker auto-repair are on.
- Add the Buy Me a Coffee block (memory `reference_bmc_button.md`) if wanted on this page.
