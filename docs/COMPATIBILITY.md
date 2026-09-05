# Compatibility report

## Reference Omarchy system

Verified through September 4, 2026:

| Component | Version | Result |
| --- | --- | --- |
| Omarchy | 4.0.0.r1979.gb686ed8-1 | Pass |
| Hyprland | 0.56.2 | Pass |
| Linux | 7.1.11-arch1-1 | Pass |
| Qt | 6.11.2 | Pass |
| SDL | 3.4.14 | Pass |
| Native Steam | 1.0.0.87-3 | Library, artwork, launch delegation, and local achievements pass |
| Native Faugus | 2.2.1-1 | Binary and delegated launch command contract pass |
| Native RetroArch | 1.22.2-5 | Signed Arch binary and `-L` CLI contract pass |

The reference library contains 45 installed Steam games. Theme colors, font,
launcher transparency, one-click details, keyboard navigation, and the
controller input path have been exercised on this system.

## 1.6 release validation

Published 1.6.0 is commit `c91b14e40437a14849f37c188d5762c12655299e`. The
maintainer tested the exact installed candidate, including the controller-focus
fix, and approved publication. All 41 release tests pass locally and on x86_64
and aarch64 CI. The preceding candidate also passed all 41 Debug tests.

Automated coverage includes Couch Mode detail and grid layouts, focus paths,
held navigation, controller reconnects, keyboard and mouse handoff, cursor
visibility, and a cached 1,000-game library. Regressions cover layout selection,
empty states, background controller input, GOG ownership after inventory errors,
and removing the last direct GOG game.

Both architecture packages passed lifecycle checks and dependency scans. Public
checksums and provenance were verified against the release commit. The public
x86_64 package also passed upgrade from 1.5.0, removal, reinstall, and smoke tests
in a disposable Arch container. This does not replace real Omarchy ARM64 hardware
or native/Flatpak library reports. Those gaps remain open with maintainer approval.

## Main after 1.6.0

The role-name cleanup in PR #28 merged as `2de11d5`. All nine models retain
identical role IDs and names, and all 41 release tests pass on the integrated
code. This is an unreleased maintenance change, not an update to the 1.6.0
packages. It does not add hardware or real-library compatibility evidence.

## 1.7 candidate validation

The completion candidate passes 81 automated checks covering personal-data
migration, backup recovery, controller flows, offscreen layouts, and a cached
large-library fixture. The maintainer confirmed the installed grid fix looks
good. Physical controller, broader real-launcher, and native ARM64 validation
remain separate from these checks. See
[COMPLETION-PROGRESS.md](COMPLETION-PROGRESS.md) for evidence and limitations.

## Automated visual matrix

Verified offscreen on August 31, 2026:

| Fixture | Size | Result |
| --- | --- | --- |
| Catppuccin Latte light theme | 820 × 590 | Pass |
| Osaka Jade dark theme | 1380 × 880 | Pass |
| Everforest at 1.25 scale | 1380 × 880 physical | Pass |
| Tokyo Night ultrawide | 2560 × 1080 | Pass |
| No compositor blur | 820 × 590 | Pass |

These deterministic renders verify layout, clipping, card aspect ratios, and
theme contrast without changing the active desktop. A render smoke test runs in
CI. Additional real-user reports expand compatibility coverage after v1.

## Contract-tested sources

Lutris native and Flatpak discovery, Heroic native and Flatpak discovery,
Faugus and RetroArch native and Flatpak discovery, PCSX2 and Ryujinx scanner
contracts (native and Flatpak roots), direct GOG manifests and launch tasks,
Epic, GOG, and Amazon manifests, and
Battle.net product.db discovery across Wine, Proton, and Bottles
prefixes are covered by repeatable local fixtures. These
paths still need reports from users with those launchers installed before the
stable release gate can close.

## Still needed

- A clean Omarchy installation
- Native and Flatpak Lutris libraries from real users
- Native and Flatpak Heroic libraries from real users
- A configured native or Flatpak Faugus library from a real user
- A configured native or Flatpak RetroArch library from a real user
- A Battle.net library from a real Wine, Proton, or Bottles prefix
- Steam Flatpak from a real user
- Light, scaled, ultrawide, and blur-disabled checks on real displays

Reports should follow [SUPPORT.md](../SUPPORT.md) and must not include secrets.
