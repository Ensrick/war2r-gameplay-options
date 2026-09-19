<!-- GitLab picks this up as the "Bug" description template. Keep it in step with
     .github/ISSUE_TEMPLATE/bug_report.md -->

**What happened, and what did you expect instead?**


**How to make it happen again** (mission or custom map, your race, which units, which setting or spell)


**Versions**
- Mod version (the first line of `x86\gameplay_options.log`):
- Game build (the mod supports 1.0.2.2818):

**Please attach these files** (drag them into this box; a `.txt` copy is fine)
- `Warcraft II Remastered\x86\gameplay_options.log` from the session where it happened
- your `Warcraft II Remastered\x86\gameplay_options.toml`
- if the game crashed: the newest folder in `Warcraft II Remastered\x86\Errors\` (`Crash.txt` and the `.dmp`)

**More detail in the log:** set `log_casts = true` under `[general]`, play until it happens again, and attach that
log. It then writes a line for every spell cast, every unit auto-production starts, and a line saying why nothing
was produced.

**No `gameplay_options.log` at all?** Then the game you started is not the copy the mod is in. Check that your
shortcut starts `Warcraft II.exe` from the same `x86` folder that holds `version.dll`.

/label ~bug
