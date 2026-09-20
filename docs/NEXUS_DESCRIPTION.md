# Nexus page material

Nexus page (created by the author 2026-09-18): https://www.nexusmods.com/warcraft2/mods/8 (game domain `warcraft2`, mod id 8).

The Nexus texts live in `nexus/` as plain BBCode text files, ready to paste:

| File | Goes into |
|---|---|
| `nexus/NEXUS_SUMMARY.txt` | the short summary line (350 characters max) |
| `nexus/NEXUS_DESCRIPTION.txt` | the description editor in BBCode mode: installation and how to use `gameplay_options.toml` |
| `nexus/NEXUS_COMMENT_WELCOME.txt` | the comments tab: a welcome, what to attach for a bug, how to ask for a feature (post once, then pin it) |
| `nexus/NEXUS_CHANGELOG.txt` | the changelog / release notes |

Keep them in step with `docs/USER_README.txt`, `docs/CONFIG_TUTORIAL.md` and `CHANGELOG.md` whenever a setting changes.

## State (2026-09-18)

- The GitHub repository is PUBLIC (the author's call, "so people can report issues"). License: MIT since
  1.0.10 (open source). The permission dropdowns on the Nexus page are the author's to set on the website; to agree
  with MIT they should allow uploads elsewhere, modification, conversion and asset use, each "with credit".
- `nexus/NEXUS_DESCRIPTION.txt` changed with 1.0.9 / 1.0.10 (GitHub, license and bug report paragraph): the description cannot be edited
  through the Nexus API, so it has to be pasted again on the website.
- On Nexus: **1.8.0** (file id 17, MAIN, 2026-09-20 00:17; 1.7.2, 1.4.1, 1.4.0 and the author's 1.0.0 are archived).
  Changelog entries exist for 1.0.0 to 1.7.2. The endpoint is append-only: read
  `GET /v1/games/warcraft2/mods/8/changelogs.json` first and skip every version that already has one. The page's
  version field lags the upload by a while (it still showed 1.4.1 right after this one).
- Every Nexus upload also gets a GitHub release: tag `v<version>` on the release commit, the same zip attached, notes =
  that version's CHANGELOG.md section (first one: v1.4.1).
- Future uploads still need the author's go-ahead for that version.
- Shipped config: examples as comments; only Heal, Slow, Bloodlust, Raise Dead and worker auto-repair are on.
- Add the Buy Me a Coffee block (memory `reference_bmc_button.md`) if wanted on this page.
