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

- The GitHub repository is PUBLIC (the author's call, "so people can report issues"). License: the custom
  source-available `LICENSE`, same conditions as the permissions set on the Nexus page. Do not call it open source.
- `nexus/NEXUS_DESCRIPTION.txt` changed with 1.0.9 (GitHub / bug report paragraph): the description cannot be edited
  through the Nexus API, so it has to be pasted again on the website.
- On Nexus: 1.0.0 (the author's own upload). Nothing newer is uploaded; uploads wait for the author's go-ahead naming
  the version. At upload time post one changelog entry per version that Nexus does not have yet
  (`nexus/changelog_<ver>_api.txt`; the API is append-only).
- Shipped config: examples as comments; only Heal, Slow, Bloodlust, Raise Dead and worker auto-repair are on.
- Add the Buy Me a Coffee block (memory `reference_bmc_button.md`) if wanted on this page.
