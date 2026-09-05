# Omakade product and delivery plan

Implementation status: M0 through M7 are complete. Steam, GOG, Lutris,
Heroic, Faugus, RetroArch, PCSX2, Ryujinx, and Battle.net import, launch
delegation, source filters, organization, settings, release checks, explicit
linking, RetroAchievements, and Sunshine/Moonlight integration are implemented.
M6 shipped in 1.6.0; remaining hardware and real-library validation is tracked below.

## Product statement

Omakade is a beautiful, local-first game library built for Omarchy. It brings
installed games from Steam, GOG, Lutris, Heroic, Faugus, RetroArch, PCSX2,
Ryujinx, and Battle.net into one coherent place.
It owns discovery, presentation, search, achievements, organization, and the
launch action. Existing platforms continue to own authentication, installation,
updates, compatibility tools, cloud saves, DRM, and overlays.

Tagline: **Your games, beautifully together.**

## Product boundaries

### Omakade owns

- A unified library and game identity
- Cover, hero, logo, and achievement presentation
- Search, filters, favorites, hidden games, and collections
- Local metadata indexing and cache management
- Keyboard, mouse, and controller navigation
- Launch delegation and useful failure messages
- Omarchy theme, font, window, and menu integration
- Optional online metadata and achievement connections

### Omakade delegates

- Game installation and updates
- Steam, Epic, GOG, Amazon, and other account authentication
- Proton, Wine, and runner configuration
- DRM, cloud saves, multiplayer, friends, and platform overlays
- Purchasing and store browsing

This boundary is the main scope control. Omakade is a first-class gaming home,
not a reimplementation of Steam, Heroic, or Lutris.

## Initial assumptions

- Omarchy on Arch Linux, Wayland, and Hyprland is the primary target.
- Omakade remains a standalone open-source application.
- Steam is the only source in the first usable release.
- Installed games work without an account login, API key, or network access.
- The initial interface is a cover-first desktop window with an optional
  fullscreen mode.
- Every action is designed around a shared focus model from the beginning so
  controller support does not require a later UI rewrite.
- Omakade reads user data but never modifies Steam, Heroic, or Lutris libraries.
- Imports are repeatable and safe. Removing an entry from Omakade never
  uninstalls the underlying game.

## Success criteria

The first public release is successful when a new Omarchy user can:

1. Install Omakade through a normal Arch package.
2. Open it from the Omarchy application launcher.
3. See every locally installed Steam game without configuration.
4. See local Steam artwork or an intentional fallback for every game.
5. Search and navigate the library with keyboard, mouse, or controller.
6. Launch a game through Steam and receive a useful error when Steam cannot
   launch it.
7. Change the Omarchy theme or font and see Omakade update without restarting.
8. Use Omakade offline for all core library and launch behavior.

## Experience principles

### Cover art is the content

The chrome stays quiet. Covers, heroes, logos, achievements, and play history
carry the visual weight. Empty space and translucency should frame artwork, not
compete with it.

### Omarchy-native, not Omarchy-themed

Omakade must inherit Omarchy's semantic palette, fontconfig monospace alias,
rounding, spacing rhythm, input behavior, and Hyprland conventions. It should
feel related to the shell even when no Omarchy logo is visible.

### Fast before decorative

The library appears from the local database immediately. Rescans, artwork
decoding, and network refreshes happen away from the UI thread. Animation never
delays input.

### Honest platform boundaries

Buttons say what will happen. "Play" launches through the owning platform.
"Manage in Steam" opens Steam. Missing credentials or private Steam data are
explained without implying that the library is broken.

### One interaction model

Hover, keyboard focus, controller focus, pressed state, and selection are
related but visually distinct. The user never loses their place when moving
between a grid and a detail view.

## Core user journeys

### First launch

1. Omakade detects Steam and known library roots.
2. The cached library shell renders immediately.
3. A background scan imports installed app manifests and local artwork.
4. Covers appear progressively without moving focused cards.
5. The user lands on Recently Played when history exists, otherwise All Games.

There is no onboarding wizard unless automatic discovery fails.

### Find and play

1. Open Omakade from the Omarchy launcher.
2. Type to search immediately or navigate the cover grid.
3. Open game details or use the card's primary Play action.
4. Omakade delegates to Steam using the game App ID.
5. Omakade records the launch request and follows the configured post-launch
   behavior.

### Inspect achievements

1. Open a Steam game's details.
2. If Steam achievements are connected, show completion, unlock dates, hidden
   achievement treatment, and global rarity.
3. If no API key is configured, show a small optional connection action.
4. If profile privacy blocks the data, explain the exact limitation.

### Change the OS theme

1. The user changes an Omarchy theme or font normally.
2. Omakade observes the active theme state directory and fontconfig change.
3. Palette, control states, typography, and window surfaces update live.
4. Artwork remains stable while only Omakade chrome transitions.

## Visual system

### Palette

The source of truth on Omarchy is:

`~/.local/state/omarchy/current/theme/colors.toml`

Initial semantic mapping:

| Omakade role | Omarchy token |
| --- | --- |
| Window canvas | `darker_background` |
| Main translucent surface | `background` |
| Elevated card or panel | `lighter_background` |
| Primary text | `bright_foreground` |
| Body text | `foreground` |
| Muted text | `dark_foreground`, adjusted when contrast is insufficient |
| Focus and primary action | `accent` |
| Selection | `selection` |
| Success | `green` |
| Warning | `yellow` |
| Destructive or unavailable | `red` |

Rules:

- Do not hardcode a separate palette for each stock theme.
- Validate text contrast at runtime and fall back to a safer foreground token.
- Use alpha on Omakade surfaces, not on text or artwork.
- Provide a built-in dark fallback outside Omarchy.
- Watch the parent state directory because Omarchy replaces the current theme
  directory atomically during a theme change.

### Typography and density

- Resolve the `monospace` fontconfig alias so Omakade follows `omarchy font`.
- Start with the Omarchy shell's 12 px base and proportional type scale.
- Read useful spacing and font overrides from the active `shell.toml` only after
  the palette integration is stable.
- Follow `[launcher].background-alpha` from the active `shell.toml` for the
  primary window surface, with a readable built-in fallback outside Omarchy.
- Respect display scaling and never assume 96 DPI.
- Use tabular numerals for playtime and achievement percentages.

### Shape and depth

- Query Hyprland rounding at runtime when available.
- Match the shell's compact spacing rhythm instead of importing a generic
  Material or Fluent component style.
- Use one quiet border treatment and one strong focus treatment.
- Use compositor transparency as enhancement. The UI must remain readable when
  Hyprland blur is disabled.
- Avoid nested glass panels and excessive shadows.

### Motion

- Keep common transitions between 120 and 220 ms.
- Animate focus, selection, detail entry, and artwork loading only.
- Never animate the whole grid during a metadata refresh.
- Offer reduced motion and honor it throughout the component library.
- Keep focus movement responsive even while a transition is running.

### Primary surfaces

1. **Library:** adaptive cover grid, source and collection filters, immediate
   search, sort control, and stable focus.
2. **Game details:** hero image, logo or title, Play, playtime, source, recent
   activity, achievements, and a compact overflow menu.
3. **Achievements:** completion summary and a responsive badge grid with rarity
   and unlock date.
4. **Settings:** sources, optional services, artwork cache, appearance behavior,
   controller hints, and diagnostics.
5. **Empty and error states:** intentional artwork fallbacks with one clear next
   action.

## Technical architecture

### Stack

- C++20 application core
- Qt 6 and QML/Qt Quick UI
- CMake build
- Qt SQL with SQLite for the local index
- Qt Network for optional HTTP integrations
- Qt DBus for single-instance activation and desktop integration where useful
- Qt Concurrent or worker objects for scans and image work
- SDL 3 considered for normalized controller input after the keyboard focus
  model is proven

Why this stack:

- Omarchy's shell already uses Qt/QML, so the rendering and interaction model
  are a natural fit.
- Qt Quick provides a GPU-backed scene graph, animation, model/view support,
  high-DPI behavior, and strong Wayland support.
- A C++ core keeps platform scanning, SQLite, process launching, and QML models
  direct without adding a web runtime.
- Omakade stays a separate process. A crash cannot take down the Omarchy shell.

### Proposed repository layout

```text
omakade/
  CMakeLists.txt
  LICENSE
  README.md
  PLAN.md
  cmake/
  packaging/
    arch/
    desktop/
  resources/
    icons/
  src/
    app/
    library/
    sources/
      steam/
    storage/
    theme/
    launch/
    network/
  qml/
    components/
    screens/
    styles/
  tests/
    fixtures/
    unit/
    qml/
    integration/
```

Do not introduce a plugin system in the first release. Build a concrete Steam
source. Extract a small source interface only when the second source proves the
shared contract.

### Runtime flow

```text
Steam files -> Steam scanner -> normalized records -> SQLite -> QML models
                                               |
Local artwork -> artwork resolver -> cache ----+

User action -> launch service -> owning platform URI or command

Omarchy theme state -> theme reader -> semantic design tokens -> QML
```

### Data model

Keep game identity separate from a platform installation from the beginning:

- `games`: Omakade identity, display title, sort title, favorite, hidden
- `installations`: source, source game ID, installed state, launch target,
  library path, last observed timestamp
- `artwork`: game, kind, local path, provider, dimensions, selected priority
- `play_activity`: installation, locally observed playtime, last played, last
  launch request
- `achievements`: installation, API name, title, description, icon paths,
  unlocked state, unlock time, rarity
- `collections` and `collection_games`: user organization
- `source_state`: scan cursor, source version, last successful scan, error

For Steam-only v1, one game maps to one Steam installation. Do not implement
automatic fuzzy duplicate merging until two real sources exist. A bad merge is
worse than two visible entries.

### Storage

Follow XDG paths:

- Config: `$XDG_CONFIG_HOME/omakade/config.toml`
- Data and SQLite: `$XDG_DATA_HOME/omakade/`
- Artwork and HTTP cache: `$XDG_CACHE_HOME/omakade/`
- Logs and temporary state: `$XDG_STATE_HOME/omakade/`

API credentials go through the system Secret Service when available. They are
never written to config, the database, logs, crash reports, or command lines.

Schema migrations are forward-only, transactional, and tested from every
released schema version. Package removal does not delete user data.

## Steam integration

### Local discovery

- Detect native package and Flatpak Steam roots separately.
- Read Steam library roots and `appmanifest_*.acf` records.
- Import installed games only in the first release.
- Resolve Steam's local library artwork and user-selected custom grid artwork.
- Treat all Steam files as untrusted input and tolerate partial writes.
- Watch manifest and artwork directories for changes, with a debounced rescan.
- Never write to Steam VDF, ACF, userdata, or artwork directories.

Parsing gets synthetic fixtures for:

- One default library
- Multiple library drives
- Missing or malformed manifests
- Escaped VDF strings
- Flatpak paths
- A library disappearing during a scan
- Custom artwork overriding official artwork

### Launching

- Delegate Steam games through a Steam launch URI using the App ID.
- Ask the desktop to open the URI instead of shell-building a command.
- Report missing Steam, invalid App ID, and URI launch failure distinctly.
- Do not wait for the Steam process to exit as a proxy for game lifetime.
- Add "Manage in Steam" as a separate action.

### Artwork priority

1. User-selected Omakade artwork
2. User-selected Steam custom artwork
3. Steam's local cached artwork
4. Optional provider artwork
5. Generated Omakade fallback

Image work is asynchronous. Store source files once and generate size-aware
cache variants without upscaling poor assets unnecessarily.

### Achievements and account data

Achievements are an optional connected feature after the offline library is
solid.

- Ask the user for a Steam Web API key and Steam ID explicitly.
- Store the key in Secret Service.
- Use Valve's documented player achievements and schema endpoints.
- Cache responses with conservative refresh intervals.
- Respect private profiles and rate limits.
- Show stale cached data when offline and label its last refresh.
- Never scrape Steam community pages or depend on undocumented login cookies.

Owned games and account-wide playtime can be added through documented APIs, but
must not replace local installed-game discovery.

### Optional game insights

- Prefer IGDB as a single attributed provider for external critic aggregates
  and rushed, normal, and completionist time estimates.
- Store the user's Twitch application credentials in Secret Service and cache
  results locally. Never ship a shared client secret.
- Label values as IGDB data. Do not present them as OpenCritic, Metacritic, or
  HowLongToBeat scores.
- Never scrape critic or completion-time websites.
- Keep insights optional so startup and the core library remain offline-first.

## Later source integrations

### Lutris

- Discover through Lutris's documented JSON game listing where possible.
- Launch using its documented protocol or stable command interface.
- Import installed entries only at first.
- Keep runner configuration in Lutris.

### Heroic

- Read Heroic's public local manifests through a versioned adapter.
- Launch through Heroic's supported launch protocol.
- Contract-test every supported Heroic release because local formats and launch
  links can change.
- Keep installation, accounts, Wine settings, and cloud saves in Heroic.

### GOG

- Discover installed games using bounded `goggame-*.info` manifests,
  including existing Heroic-managed GOG installations.
- Treat manifest paths as untrusted input and confine executable and working
  directory resolution to the installation directory.
- Launch native Linux builds directly and Windows builds using UMU with an
  isolated per-game prefix.
- Keep Heroic-managed GOG installs delegated to Heroic so their runner,
  environment, wrapper, and script settings remain intact.
- Keep account authentication, purchasing, installation, updates, and cloud
  saves outside Omakade.

### Desktop applications and manual games

- Add this only after Steam, Lutris, and Heroic are reliable.
- Use desktop-entry parsing instead of arbitrary shell strings.
- Require explicit confirmation before importing general applications.
- Represent direct native executables as a separate source with a visible
  security warning when edited.

### Retro games

- Import configured RetroArch playlists, local thumbnails, core associations,
  and runtime logs without crawling arbitrary ROM folders.
- Do not become an emulator manager in the first major release.
- RetroAchievements is a separate optional connection.

### Battle.net

- Discover Wine, Proton, and Bottles prefixes that contain Battle.net's Agent
  `product.db`. Do not crawl arbitrary home folders.
- Import installed products only, skipping the Battle.net agent and app.
- Launch through the Battle.net client in the same prefix. Keep authentication,
  installation, updates, and DRM in Battle.net.

## Omarchy operating-system integration

### Application identity

- Reserve the final reverse-DNS application ID before the first release.
- Ship a matching desktop entry, icon name, Wayland app ID, and DBus name.
- Support activate-or-focus so repeated launcher invocations do not create
  duplicate windows.
- Open as a normal tiled window, remember size, and provide fullscreen. Do not
  force floating or fullscreen through compositor rules.

### Theme integration

- Read the active Omarchy palette directly for zero-setup operation.
- Watch active theme state for live updates.
- Resolve the current font through fontconfig.
- Query current Hyprland rounding and gaps when available.
- Fall back cleanly on non-Omarchy desktops.
- Add an Omarchy-generated Omakade theme file only if direct semantic mapping
  proves insufficient. Do not require a user hook for basic theme following.

### Hyprland integration

- Use alpha-capable Qt Quick surfaces and test with blur both enabled and
  disabled.
- Set a stable Wayland app ID so rules can be narrowly scoped.
- Request no global keybinding in the standalone package.
- If Omarchy later adopts a shortcut, add it upstream where conflicts and help
  text can be managed centrally.
- Verify current Hyprland window-rule syntax immediately before adding any rule.

### Omarchy menu integration

The eventual upstream change should be small:

- `Install > Gaming > Omakade`
- Matching `Remove > Gaming > Omakade`
- Package installation through Omarchy's package helpers
- A normal desktop entry discoverable through the main launcher
- Optional acceptance coverage that launches, focuses, and closes Omakade

Omakade application source should not live inside the Omarchy repository.

## Security and privacy

- Local-first by default, with no analytics or required account.
- Network providers are individually enabled and clearly named.
- Validate all parsed paths and never execute values read from library files.
- Launch through structured URI or process APIs, never a shell command assembled
  from metadata.
- Bound image dimensions and decoded memory before loading untrusted artwork.
- Use TLS through Qt Network and redact credentials from diagnostics.
- Make cache clearing visible and predictable.
- Document every external request and retained field.
- Add dependency scanning and a release bill of materials later, before broad
  distribution.

## Performance and quality targets

These are engineering targets to measure, not release claims:

- Useful cached library visible within 500 ms on the reference Omarchy desktop
- No filesystem scan or network request on the UI thread
- Smooth 60 fps navigation through a synthetic 1,000-game library
- Stable card positions while artwork resolves
- Idle memory target below 150 MB with a representative library
- Search response within one frame after debounce
- No required network access during startup
- No unbounded artwork or API cache

## Accessibility and input

- Every action reachable without a pointer
- Predictable focus restoration when returning from details
- Visible focus that survives every Omarchy palette
- Controller glyphs based on the active controller family when known
- No information communicated by color alone
- Runtime contrast checks for text and focus borders
- Reduced-motion mode
- Screen-reader names, roles, and descriptions for custom QML controls
- Scalable type without clipping at common 125%, 150%, and 200% display scales

## Testing strategy

### Unit tests

- VDF and ACF parsing
- Path discovery and Flatpak/native separation
- Game normalization and sort titles
- Theme parsing, token mapping, and contrast fallback
- Launch target validation
- Database migrations and transactions
- Cache limits and eviction

### QML component tests

- Focus, hover, selected, and pressed states
- Keyboard navigation and focus restoration
- Grid resizing and delegate reuse
- Missing, loading, and broken artwork
- Reduced motion
- Long titles and localization expansion

### Integration tests

- Import a synthetic multi-library Steam fixture
- Re-import without creating duplicates
- Observe an installed or removed manifest
- Open a captured launch URI without executing a real game
- Switch between representative light and dark Omarchy theme fixtures
- Start twice and verify activate-or-focus

### Visual acceptance

Capture the same library states in at least:

- Tokyo Night
- Catppuccin
- Vantablack
- White or another light theme
- One user-created theme

Verify at 1080p, 1440p, ultrawide, and scaled displays. Review library, details,
achievements, settings, empty, offline, and error states.

### Real-system acceptance

- Native Steam package
- Flatpak Steam
- Multiple Steam library drives
- Steam closed before launch
- Offline session
- Blur disabled
- Theme changed while Omakade is open
- Controller connected and disconnected while Omakade is open

Upstream Omarchy acceptance runs belong in a disposable VM, not an active user
session.

## Packaging and release

### Development releases

- Build with CMake presets.
- Run formatting, static analysis, unit tests, QML tests, and a headless startup
  smoke test in CI.
- Produce a versioned Arch package artifact for testers.

### First public release

- Publish source and checksummed release artifacts.
- Maintain an AUR package using the normal stable release tarball.
- Ship desktop entry, scalable icon, AppStream metadata, license, and changelog.
- Keep dependencies to required Qt modules, SQLite, and the chosen controller
  library.
- Document native package support first. Add Flatpak or AppImage only when their
  filesystem permissions and launch behavior are tested properly.

### Versioning

- Use semantic versioning after `1.0`.
- Before `1.0`, each minor release may migrate the database but must preserve
  user favorites, artwork choices, and collections.
- Publish short, human release notes focused on visible changes and known limits.

## Upstream path into Omarchy

1. Release Omakade independently and package it for Arch.
2. Collect screenshots, startup measurements, supported Steam layouts, and real
   user feedback.
3. Open an Omarchy Suggestions discussion with the working application, not a
   concept pitch.
4. Ask first for optional inclusion under `Install > Gaming`.
5. Keep the Omarchy pull request limited to install/remove integration, menu
   entries, and acceptance coverage.
6. Follow the Omarchy repository's current `AGENTS.md` and run its required test
   suite in the prescribed VM workflow.
7. Consider default installation only after sustained adoption and explicit
   maintainer interest.

Do not brand Omakade as an official Omarchy application before approval.

## Delivery milestones

### M0: Foundation and visual proof

Deliver:

- Repository, license, build, formatting, and test harness
- Qt Quick window with final application identity placeholder
- Design-token layer with live Omarchy palette and font following
- Mock 100-game library with deterministic artwork fixtures
- Library grid, focus model, detail transition, and responsive layout
- Initial icon and wordmark direction

Gate:

- Runs natively on Wayland
- Theme changes apply live
- Keyboard navigation is complete
- Synthetic 1,000-game grid meets the frame target
- Visual captures approved across representative themes

### M1: Local Steam MVP

Deliver:

- Native and Flatpak Steam discovery
- Library and manifest parsing
- SQLite index and safe repeatable rescans
- Local and custom artwork resolution
- Search, favorites, hidden games, installed filter, and sorting
- Steam launch and Manage in Steam actions
- Useful empty and error states

Gate:

- A clean Omarchy install requires no setup
- Multiple library fixtures import without duplicates
- Core behavior works offline
- Omakade never writes into Steam data
- Launch behavior is verified without shell interpolation

This is the first private alpha.

### M2: Input and finish

Deliver:

- Normalized controller input
- Controller glyphs and focus restoration
- Fullscreen mode
- Reduced motion and accessibility pass
- Artwork cache limits and diagnostics
- Activate-or-focus desktop behavior
- Loading, failure, and interrupted-scan polish

Gate:

- Complete find-and-play journey works without keyboard or mouse
- Hot-plug does not lose focus or crash
- Display scaling and representative screen sizes pass visual review
- Startup, memory, search, and grid measurements meet targets

This is the first public preview.

### M3: Steam depth

Status: complete in the 0.3 preview. Steam's local achievement cache is the
zero-setup primary source. Secret Service and the documented Steam Web API are
an optional enrichment path.

Deliver:

- Secret Service credential storage
- Steam achievement schema and player progress
- Completion, rarity, unlock dates, and offline cache
- Optional owned-library and account playtime enrichment
- Clear privacy and API-key setup UX

Gate:

- Private, public, offline, invalid-key, and rate-limited states are tested
- No credential reaches config, database, logs, or process arguments
- Cached achievement data remains usable offline

### M4: Unified library

Status: complete in the 0.5 preview. Lutris and Heroic installed-game import
and launch, the shared model, source filters, persistent user-selected cover
overrides, and explicit duplicate linking are complete.

Deliver in this order:

1. Lutris installed-game import and launch
2. Heroic installed-game import and launch
3. Source filters and source-specific management links
4. Explicit linking of duplicate games across sources
5. Desktop-entry or manual native games if still needed after the stable release

Gate for each source:

- Import is repeatable
- Launch uses a documented or contract-tested interface
- Missing source applications degrade cleanly
- Removing an Omakade entry does not alter the source library
- Source format fixtures are versioned and tested

### M5: Stable release and Omarchy proposal

Status: complete for v1.0. The release is local-first, source launchers retain
ownership of installs and accounts, and every release gate has automated or
documented acceptance evidence.

Deliver:

1. **Public foundation**
   - Public GitHub repository, project homepage, screenshots, topics, and About
   - Stable application ID, AppStream metadata, support and privacy docs
   - CI build, tests, metadata validation, and source release process
2. **Library depth**
   - Optional attributed metadata, artwork, critic score, and game-length cache
   - Collections, completion status, tags, and useful smart filters
   - Correct recent activity and launch history across supported sources
3. **Trust and control**
   - Settings for sources, services, cache, appearance, and post-launch behavior
   - Source health with last scan, discovered paths, and actionable errors
   - ProtonDB and PCGamingWiki links, controller status, and launch failures
4. **Distribution**
   - Reproducible Arch package, clean install, upgrade, and uninstall tests
   - Checksummed release artifact and short release notes
   - Real-system compatibility report and focused Omarchy Suggestions post
   - Small optional install/remove integration pull request if invited

Gate:

- A first-time Omarchy user can install, discover, browse, and launch without
  setup or a terminal
- Every supported source reports healthy, unavailable, or failed without silent
  omissions
- Core behavior remains useful offline and no optional service blocks startup
- Existing 0.5 user data survives the v1 database migration
- Keyboard, mouse, and controller complete the main find-and-play journey
- Light, dark, blur-disabled, scaled, and ultrawide visual checks pass
- Clean package install, upgrade, removal, and reinstall pass in a disposable VM
- No known critical crash, credential exposure, or data-loss issue remains

Gate:

- No known data-loss or credential issues
- Upgrade from every public preview is verified
- Maintainer response path exists
- Visual evidence and acceptance results are ready for upstream review

### M6: Controller-first couch mode

Status: shipped in 1.6.0, with review fixes covered by regression checks. The dedicated ten-foot interface supports television use.

Deliver:

- A visually rich fullscreen home, library, and game-detail experience built
  around cover and hero art, readable typography, and restrained motion
- Complete controller access to browsing, search, filters, sorting, collections,
  favorites, hidden games, achievements, settings, dialogs, and launch actions
- Fast spatial navigation with obvious focus, no dead ends, persistent button
  hints, controller-family glyphs, and correct focus restoration
- Controller-friendly search and text entry, including an on-screen keyboard
- A clear way to enter couch mode, remember the preferred launch mode, and
  switch back to the prior desktop layout without losing context
- Seamless controller disconnect, reconnect, keyboard, and mouse handoff
- Layouts that remain polished at 1080p, 1440p, 4K, ultrawide, and supported
  scaling levels, including reduced-motion and blur-disabled configurations
- Sunshine and Moonlight behavior that opens directly into the couch experience

Gate:

- The full browse, search, inspect, organize, configure, and launch journey works
  from a couch without reaching for a keyboard or mouse
- Every interactive surface passes a controller focus-path sweep with no traps,
  unreachable controls, or surprising directional jumps
- Core journeys pass at common television resolutions and 200% scaling, with
  readable contrast, visible focus, reduced motion, and no clipped content
- A 1,000-game cached library remains responsive and starts within the existing
  performance targets
- Leaving couch mode restores the prior desktop layout and focus position

Release status:

- The exact published candidate passes 41 of 41 release tests locally and on both
  CI architectures. The preceding candidate also passed all 41 Debug tests.
- Detail and grid views, clear selection, held analog and directional-pad
  navigation, cursor handoff, reconnect behavior, and large-library paths have
  automated coverage.
- The maintainer tested and approved published commit `c91b14e`. Controller-focus
  and GOG cache fixes are covered by regression tests. Package lifecycle checks,
  dependency scans, public checksums, and signed provenance pass.

### M7: Sunshine and Moonlight streaming

Status: the 1.4 feature. Omarchy installs Sunshine as a user service and ships
Moonlight, and Sunshine's stock app list is only Desktop and Steam Big Picture.

Deliver:

- `omakade --play Source:runner:id` and `omakade --quit`, forwarded to the
  running window through the single-instance socket or run headless from the
  cached library
- Opt-in export of Omakade and of every installed game into Sunshine's
  `apps.json` as detached entries with PNG box art, marked so only Omakade's
  own entries are ever rewritten, with a one-time backup and a user-service
  restart action
- Fullscreen when Sunshine launches Omakade for a Moonlight client
- Native and Flatpak Sunshine paths, with `flatpak-spawn --host` for the
  sandboxed one

Gate:

- Foreign Sunshine entries and the `env` block survive every sync byte for
  byte
- A play request for a hidden, linked, or RetroArch game resolves the same
  installation Play would
- Nothing is written when `apps.json` does not exist yet; Sunshine creates it

Couch mode (M6) is the headline 1.6 feature, building on this streaming work.

### Current roadmap, September 5, 2026

Version 1.6.0 shipped on September 5 at `c91b14e`, including Couch Mode,
direct GOG support, ARM64 packages, dependency scanning, and release SBOMs.
The earlier milestone sections describe the development history.

The active post-1.6 completion scope and acceptance gates are in
[COMPLETION-PLAN.md](docs/COMPLETION-PLAN.md), with execution evidence in
[COMPLETION-PROGRESS.md](docs/COMPLETION-PROGRESS.md). Convenience features
are local candidate work until maintainer testing and publication approval.
Real launcher reports (#9) and ARM64 hardware evidence (#13) remain open;
published packages do not establish hardware compatibility.

The shared QML role-name cleanup from PR #28 is included in the 1.7 candidate.
It preserves role IDs and names across the existing nine game models.

## Explicitly deferred

- Installing, updating, repairing, or moving games
- Storefront browsing and purchasing
- Proton or Wine configuration
- Cloud-save management
- Friends, chat, and multiplayer invitations
- Automatic fuzzy merging across stores
- Emulator installation and ROM scraping
- Plugin marketplace or third-party executable plugins
- Background daemon
- Mobile companion
- Cross-device sync

Each item needs a separate product decision. None should enter incidentally while
building the library.

## Decisions to settle before M0 implementation

Recommended defaults are listed first:

1. **License:** GPL-3.0-or-later, or MIT if permissive reuse is more important.
2. **Application ID:** reserve a reverse-DNS ID tied to the eventual project
   organization before packaging.
3. **Post-launch behavior:** close Omakade after a successful launch, with a
   future preference to keep it open.
4. **Controller library:** evaluate SDL 3 against direct Linux input before
   adding the dependency.
5. **Brand assets:** create a simple code-native SVG mark that remains legible
   in the Omarchy launcher and on game-detail surfaces.

## Reference contracts

- Omarchy theming: https://omarchy.org/manual/making-your-own-theme/
- Omarchy source: https://github.com/basecamp/omarchy
- Qt Quick: https://doc.qt.io/qt-6/qtquick-index.html
- Qt SQL and QML models:
  https://doc.qt.io/qt-6/qtquick-modelviewsdata-sqlmodels.html
- Steam achievements API:
  https://partner.steamgames.com/doc/webapi/ISteamUserStats
- Steam owned games API:
  https://partner.steamgames.com/doc/webapi/IPlayerService
- Lutris client and command interface: https://github.com/lutris/lutris
- Heroic client: https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher
