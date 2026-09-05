# Support

Please include these details with a bug report:

- Omakade version
- Omarchy version and active theme
- Native or Flatpak installation for Steam, Lutris, Heroic, Faugus, RetroArch,
  PCSX2, Ryujinx, or Battle.net (and whether Battle.net runs under Wine,
  Proton, or Bottles)
- The source shown for the affected game
- `umu-launcher` version for a direct Windows GOG game
- Steps that reproduce the problem

Open diagnostics with `Ctrl+D` to see library and cache status. Do not post a
Steam or RetroAchievements Web API key, full configuration directory, or
private account data.

Omakade can ask Steam to begin installing an owned game. Steam remains
responsible for installation, updates, moves, and removal. Problems with those
actions belong to the launcher that owns the game.

## Manual entries and restores

For a manual game, include whether it was added as an executable or desktop
entry and the error shown. Review paths and arguments before sharing them.
Windows runner setup belongs to the launcher that owns the game.

For restore failures, include the Omakade version, merge or replace choice,
and the error shown. Keep the recovery folder while diagnosing the problem.
Do not attach the archive, database, settings file, or recovery folder to a
public report; they contain personal library data and may contain private paths
or account identifiers. The startup recovery screen offers retry or undo before
the library opens. A completed restore can be reversed by previewing its saved
recovery archive as a new restore, which also previews changes made since then.
