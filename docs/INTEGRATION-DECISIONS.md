# Remaining integration decisions

Reviewed September 5, 2026 for the post-1.6 completion candidate.
These scope decisions are included for maintainer review before release.

## Prism Launcher

Disposition: defer a dedicated automatic source. The live GitHub issue inventory
contains no Prism request. Neither a prismlauncher executable nor its standard
native or Flatpak data directory is present on this machine; Flatpak itself is
not installed. Custom or remote installations have not been ruled out.

Prism documents `--launch` with the instance folder name and `--dir` for a custom
application root. A native Prism installation can be entered through Omakade's
manual executable workflow with each argument entered separately. This is a
proposed user path, not a completed real-instance compatibility test. Accounts,
Java, mods, downloads, and instance management remain in Prism.

A future dedicated source would read instance metadata and icons from native,
Flatpak, portable, and configured roots, preserve identity across disconnected
roots, and delegate to Prism with the instance ID. Before accepting that work,
require a named user need, sanitized instance fixtures, and a configured instance
for launch and return testing. Do not import credentials or manage Minecraft.

Primary contracts:
- [Prism CLI](https://prismlauncher.org/wiki/getting-started/command-line-interface/)
- [Prism data locations](https://prismlauncher.org/wiki/getting-started/data-location/)

## Steam Input and non-Steam shortcuts

Disposition: document the existing Steam-owned shortcut path; do not add automatic
shortcut writes or claim a new Steam Input integration. Steam shortcut import
already shipped before this completion candidate. Importing a game and providing
controller support for that game are separate operations.

User journey: add and configure the non-Steam game in Steam, verify its launch and
controller behavior there, refresh Omakade, and select the Steam installation.
For a linked game, make the Steam installation the preferred one when appropriate.
Omakade sends the shortcut's launch ID to Steam and leaves compatibility and input
configuration there. A direct Manual/Heroic/Lutris/Faugus launch is not automatically
converted into a Steam launch. No universal Proton or Steam Input guarantee follows.

Code evidence: SteamScanner reads shortcuts.vdf; SteamLauncher converts shortcut
IDs to the 64-bit launch ID and uses steam://rungameid. Core regressions cover
binary shortcut discovery and launch URL conversion. Preferred-installation tests
cover exact source selection overriding a linked default. These prove the contract,
not Steam Input on hardware.

Remaining acceptance: record Steam version, shortcut, controller, compatibility
selection, successful launch, in-game input, exit, and return to Omakade. Compare
launching that same shortcut from Steam and Omakade. Do not change the user's
Steam configuration or start games merely to manufacture this evidence.

Primary user instructions:
[Steam non-Steam shortcuts](https://help.steampowered.com/en/faqs/view/4B8B-9697-2338-40EC).

## Additional emulators

Disposition: no additional automatic emulator sources in this completion release.
The complete live GitHub issue list contains no outstanding named emulator-source
request. PCSX2, Ryujinx, and RetroArch are already shipped; their real-library
validation remains part of issue #9. The previously reviewed Linear project list
contains no additional emulator integration issue.

Users can add explicit native executable or supported desktop entries through the
Manual source. That does not claim discovery, ROM management, or compatibility for
an arbitrary emulator. Reopen source expansion only with a named emulator and use
case, an upstream discovery/launch contract, fixtures, and real-library acceptance.
Emulator installation, BIOS provision, ROM scraping, and runner configuration remain
outside the product boundary.
