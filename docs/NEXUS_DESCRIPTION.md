# Nexus page material

Nexus page (created by the author 2026-09-18): https://www.nexusmods.com/warcraft2/mods/8 (game domain `warcraft2`, mod id 8).

The Nexus texts live in `nexus/` as plain BBCode text files, ready to paste:

| File | Goes into |
|---|---|
| `nexus/NEXUS_SUMMARY.txt` | the short summary line (350 characters max) |
| `nexus/NEXUS_DESCRIPTION.txt` | the description editor in BBCode mode: installation and how to use `gameplay_options.toml` |
| `nexus/NEXUS_CHANGELOG.txt` | the changelog / release notes |

Keep them in step with `docs/USER_README.txt`, `docs/CONFIG_TUTORIAL.md` and `CHANGELOG.md` whenever a setting changes.

## Before publishing

- The description links to https://github.com/Ensrick/war2r-gameplay-options. That repo is PRIVATE, so the link is a
  404 for everyone else until the author decides to make it public. Visibility is the author's call.
- The description cannot be edited through the Nexus API: paste `nexus/NEXUS_DESCRIPTION.txt` on the website.
- Decided 2026-09-18: the shipped config keeps its examples as comments and only Heal, Slow, Bloodlust, Raise Dead and
  worker auto-repair are on by default.
- Add the Buy Me a Coffee block (memory `reference_bmc_button.md`) if wanted on this page.
- Uploads wait for the author's go-ahead after the Nexus page exists. No upload has been made.
