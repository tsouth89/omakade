# Completion progress

## September 5, 2026

### Baseline

- Worktree: `/home/bts/Projects/omakade-completion`
- Branch: `codex/completion`
- Released baseline: `c91b14e` (v1.6.0, peeled annotated tag)
- Original checkout remains on `codex/1.6-couch-polish` at `470d952`.
  Its model-role refactoring and Battle.net regression-test edits are untouched.
- The original checkout predates final GOG ownership and empty-library fixes.
  Starting from the release retains those fixes without mixing unrelated edits.

### Preferred installations

Implementation added:

- A separate, additive SQLite table for linked-game launch preferences.
- An explicit Make Default action and default label in game details.
- A common selection resolver for details and Sunshine exports. Explicit
  source-specific CLI launch requests retain their exact target.
- Fallback when the saved source is disabled, uninstalled, or its game folder
  is missing; the saved preference survives until it becomes available again.
- Transactional preference transfer when groups merge and removal on unlink.
- Regression coverage for persisted preference, proxy-filter mapping, invalid
  targets, disabled sources, disconnected game folders, unlink/relink, group
  merging, and exact source CLI selection.

Validation:

- Development configure and build passed.
- Initial updated development matrix: 41/41 passed, including core tests,
  controller navigation, Couch Mode, offscreen renders, and 1,000-game startup.
- Final follow-up build passed. All 6 affected CTest entries passed: the full
  core suite (including the linked preference Sunshine export regression), QML
  smoke, three desktop controller layouts, and Couch Mode navigation.
- `git diff --check` passed.
- This is implementation evidence, not maintainer acceptance. The new linked
  installation controls still need a dedicated controller/visual acceptance
  exercise and actual Steam/Lutris or Heroic launches.

### Configurable GOG folders

Implementation added:

- Extra GOG Folders in Settings, with saved paths, desktop folder selection,
  removal, and controller text entry through the existing on-screen keyboard.
- An additive combination of standard roots, saved roots, and the existing
  environment override. Saved paths allow spaces, quotes, and Unicode.
- Atomic settings writes that report failure rather than claiming a path saved.
- Queued rescanning when folder choices change during an active scan.
- Missing-folder diagnostics with cached games retained while healthy roots
  update. Reconnecting a drive restores normal scanning.
- Explicit removal clears cached discovery without deleting files or personal
  choices. Removing and re-adding a folder preserves favorites.
- Durable pending removal state so restarting before a rescan does not restore
  a folder the user removed.
- README instructions for the new settings and environment-variable behavior.

Validation:

- Development build passed. The new folder persistence and disconnected-root
  regression passed, and the full development matrix passed 41/41.
- The final full core suite passed after adding the restart-before-rescan
  regression (CTest: 1/1, 5.46 seconds). `git diff --check` passed.
- New Settings controls still need dedicated visual/controller acceptance. The
  existing matrix uses demo data and does not prove the new real-source panel.

### Manual native games and desktop entries

Implementation added:

- A persistent Manual source, with Add a Game in Settings, a desktop source
  filter, and a Couch Mode source option.
- Review-before-save import of a selected native executable or desktop entry.
  Executable, working directory, and each argument remain separately editable.
- Desktop-file values and quoting are parsed with GLib. Supported Exec field
  codes expand once; unsupported codes and terminal-required entries report
  actionable errors. Windows executable headers explain the runner handoff.
- A dedicated editor with persistent labels, individual argument fields, an
  explicit removal confirmation, and controller on-screen text entry.
- Editing retains the stable entry ID, favorites, organization, and history.
  Entries with missing executable paths stay visible for repair.
- Native launch uses separate executable/argument/working-directory values.
  Existing CLI requests and Sunshine exports resolve through the shared library.
- A source lookup fix keeps secondary linked installations addressable while
  editing. Removing an entry does not delete files or other source installations.
- README instructions explain manual games and the desktop-only file picker.

Evidence:

- The native integration test launches a temporary executable and verifies its
  actual working directory and exact arguments, including an empty argument.
- Tests cover desktop import, field expansion, saved edits, favorites, tags,
  collections, history, linked lookups, missing paths, restart, and removal.
- Dedicated desktop and Couch Mode QML tests save and edit through the actual
  form, preserve argument boundaries, and open controller text entry.
- Desktop and Couch Mode editor render fixtures were added and inspected.
  Labels and larger couch typography were refined following that inspection.
- Final development build and full matrix passed: 45/45 CTest entries in
  26.08 seconds. Final desktop and Couch Mode renders were inspected and show
  the complete form without clipping. `git diff --check` passed.
- Maintainer acceptance and real game/desktop-entry coverage remain separate
  from this temporary native fixture and offscreen editor evidence.

Reference for desktop-entry semantics:
https://specifications.freedesktop.org/desktop-entry/latest/exec-variables.html

### Artwork customization

- Added independent durable cover, hero, and logo overrides, with migration of
  existing cover records. Images are validated before saving an owned copy.
- Independent resets preserve other slots and never remove external originals.
- Added a shared artwork editor and logo display in game details with title
  fallback. Desktop and Couch Mode editor renders have been inspected.
- Regression coverage verifies old-schema migration, transparency, independent
  reset, invalid input preservation, linking/unlinking, and persistence after
  deleting the original selected image.
- Development build and all 45 CTest entries passed (51.85 seconds), including
  the new core regression. `git diff --check` passed.
- Dedicated desktop and Couch Mode tests now exercise the actual editor's apply,
  independent reset, controller text entry, and dismissal. All 9 affected core,
  controller, artwork workflow, and render CTest entries passed in 15.49 seconds.
- The Couch Mode fixture uses visual-tree lookup for repeater-created controls.
  Both editor renders fit without clipping. Real controller and maintainer
  acceptance remain required before release.

### Filtered random selection

- Pick a Game is available above the desktop library and in Couch Mode Browse.
- Chooses among available games in the current filtered results, preserving
  search, source, organization, availability, and explicit hidden filtering.
- Linked installations count once. Stable identities avoid immediate repeats
  even after sorting; a sole eligible game can be chosen again.
- Opens game details for review, with Pick Another and the existing explicit
  Play action. Empty eligible results produce a clear message.
- Manual installation availability now validates the saved native launch details
  in addition to checking file existence.
- Core tests cover installed-only eligibility, empty and single results, hidden
  filtering, missing native targets, linked identities, and sorting between picks.
- Desktop and Couch Mode QML tests activate the controls with keyboard events,
  pick another game, and dismiss the selection. The details and Browse renders
  were inspected. The new toolbar button requires an additional navigation step;
  its focus targets and existing navigation test expectations were updated.
- Development build and core suite passed. The full 52-entry matrix passed
  49 entries, exposing three old toolbar-order expectations; after correcting
  the focus path and assertions, all 6 affected navigation/random tests passed
  in 2.47 seconds. `git diff --check` passed. No failing check remains from that
  run. Maintainer acceptance remains a release gate.

### Saved filters

- Added named local views containing search, library mode, source, availability,
  hidden visibility, status, collection, tag, and sort order.
- Views have stable IDs and a versioned JSON query in the shared SQLite database.
  Case-insensitive normalized names prevent accidental duplicates. Invalid or
  future formats are rejected without changing the active query.
- Desktop Saved Filters and Couch Mode Browse open the same management screen,
  supporting save, rename, apply, and confirmed deletion. Deleting a saved view
  leaves the active query unchanged.
- Applying a view updates the full query together and retains the current game
  when it still matches. Missing collection/tag criteria remain in force with
  an explanation; recreating the dependency restores matching results.
- Controller navigation scrolls through large saved-view lists, and long names
  are truncated in the list while remaining available for editing.
- Core tests cover full-state round trips, restart, duplicate and invalid names,
  stable rename IDs, deletion, missing/recreated collections, and future formats.
- Desktop and Couch Mode QML tests save and apply through the form, exercise
  controller text entry, navigate 25 rows through a 31-view list, and close it.
- Desktop and Couch Mode render fixtures were inspected. Development build and
  all 56 CTest entries passed in 15.93 seconds; `git diff --check` passed.
- Real controller and maintainer acceptance remain separate release gates.

### Bulk organization

- Added an explicit Organize screen in desktop and Couch Mode Browse, with
  per-game selection, a visible count, Select Results, Clear, and Done.
- Selection uses stable source identities, survives sorting and source model
  rebuilds, and deduplicates selected installations when they become linked.
  A missing selected identity causes the whole action to fail without changes.
- Actions explicitly favorite/unfavorite, hide/unhide, set or clear completion
  status, add/remove tags, and add/remove collection membership. Adding a new
  collection creates it; unrelated tags and collections remain intact.
- Every batch runs in one transaction in the shared Omakade database. New
  `user_game_flags` rows hold nullable favorite/hidden overrides; absent fields
  continue to use source state. Single-game toggles honor existing overrides.
  Backup/restore must include this table along with source personal state.
- A successful batch clears selection and focuses Select Results, including
  when hiding or other changes remove the selected games from the current view.
- Core tests inject a failure after the first write and prove complete rollback.
  They also cover mixed source values, preservation of other tags/collections,
  links/unlinks, restart, source rebuilds, missing identities, and invalid inputs.
- The actual QML workflow exposed a null-status insert when first adding tags;
  that write now stores an empty status, with a regression test.
- Desktop and Couch Mode form tests navigate 25 rows, select individual games,
  open controller text entry, apply a batch that removes rows, verify focus,
  and close the editor. Both rendered layouts were inspected.
- Final development build and all 60 CTest entries passed in 17.19 seconds.
  `git diff --check` passed. Real controller and maintainer acceptance remain.

### Backup archive and snapshot foundation

- Added a version 1 ZIP archive codec with explicit personal-table and settings
  field lists, an exact artwork inventory, content-addressed image names, and
  bounded metadata, image, record, and total sizes.
- Reading validates into memory without extracting files or modifying the
  library. Unsupported versions, unexpected paths, corrupt entries, duplicate
  records, invalid images, and unlisted account settings are rejected.
- Writing uses a temporary ZIP plus QSaveFile replacement with owner-only access;
  invalid input leaves an existing backup unchanged.
- Added a separate read-only SQLite snapshot exporter. It consolidates legacy
  cached source favorites/hidden state into portable user_game_flags records,
  with non-null newer overrides taking precedence.
- Verified source identity mapping includes direct GOG versus Heroic, PCSX2's
  path-based AppId and serial Runner, and Ryujinx's Flatpak Runner.
- Personal records include disconnected/undiscovered cached identities. Legacy
  cover-only records gain empty hero/logo slots. Missing or invalid custom
  artwork fails export explicitly; missing manual executables remain portable
  launch details and are never run by the archive reader.
- Added AppSettings.backupSettings() for core preferences without account-service
  identifiers, credentials, or automatic Sunshine publishing settings.
- Tests cover archive round trips, preservation of originals and failed output,
  unsupported versions, corrupt ZIP data, unexpected entries, duplicate records,
  legacy source consolidation, correct runner identities, omitted private/cache
  data, old artwork schema, and an unchanged original SQLite file.
- Development build and the complete core CTest entry passed (5.51 seconds).
  `git diff --check` passed. Format and remaining restore requirements are recorded
  in docs/BACKUP-FORMAT.md.
- Backup/restore is not complete or exposed in the UI yet. Transactional import,
  merge/replacement preview, pre-restore backup, interruption recovery, settings
  coordination, and desktop/Couch controls remain to be implemented and tested.

### Backup database import, settings, and process recovery

- Added transactional merge and replacement for personal tables. Merge reconciles
  linked groups and preferred installations, preserves unrelated records, and
  gives conflicting saved-filter names deterministic restored suffixes. Replaying
  an import is idempotent. Replacement retains source caches and game files.
- Artwork is staged into the owned content-addressed folder. Missing manual paths
  remain repairable entries. An injected SQL failure proves whole-transaction
  rollback, including schema and earlier writes.
- Added a single-save core-settings restore method. Account-service identifiers
  and Sunshine publishing settings remain local; failed writes roll back the
  in-memory values. Fixed invalid fake client identifiers in test fixtures so
  privacy-preservation assertions exercise stored values.
- Added an internal recovery coordinator with immutable staged payloads, archive
  digests, a private pre-restore backup, exact original settings bytes, and an
  atomic journal. Failed restores stay pending. Restart replays the import or
  continues a previously chosen undo before the library can be opened.
- Child-process tests exit abruptly after each restore boundary in merge and
  replacement modes, and after both undo writes. Additional cases cover settings
  write failure, damaged recovery files, changed incoming archives, invalid
  journal paths, first-run recovery, owner-only backup permissions, and retained
  recovery history. These are process interruption tests, not power-loss tests.
- Development build passed. Core and recovery CTest entries passed together
  (5.87 seconds); recovery tests passed again after making original-settings
  fixtures valid TOML (0.46 seconds). `git diff --check` passed.
- Startup integration, preview, user-facing recovery choices, and desktop/Couch
  controls are still pending. Backup/restore remains internal and incomplete.

### Asynchronous backup preview and export service

- Added BackupManager for worker-thread snapshot export, archive validation and
  preview, and restore staging. The UI can expose busy state without blocking
  navigation during archive work.
- Preview includes current/imported/matching personal-record counts, custom
  artwork count, settings values, saved-filter name conflicts, and missing
  manual/GOG paths. Merge and replacement explanations describe data precedence
  and the recovery copy.
- Confirmation stages the exact in-memory data that produced the preview.
  Tests replace the external archive after preview and verify the reviewed
  payload is restored. Invalid previews clear previous choices, and busy or
  unavailable operations cannot queue a restore.
- Tests verify preview and staging leave live database/settings unchanged,
  account identifiers are excluded from export, protected export destinations
  remain untouched, matching counts and missing-path details are accurate, and
  name conflicts distinguish imported IDs from unrelated existing filters.
- Development build, core tests, and backup recovery/service tests passed
  (6.16 seconds for the two CTest entries). `git diff --check` passed.
- Startup recovery integration and desktop/Couch controls remain pending.

### Startup recovery and failure screen

- Integrated recovery after the normal SingleInstance claim and before source
  models, preferences, or account services are constructed. Synthetic test,
  render, demo, navigation, and stress libraries skip real recovery.
- Added an asynchronous startup controller and isolated recovery window. Success
  continues into the normal library. Failures keep the library closed and offer
  retry, undo, the private recovery folder, and close. Active writes prevent
  closing the window; interrupted processes retain journal recovery.
- Added application fixtures with owned temporary files for successful startup,
  retry after a damaged staged archive, undo, and closing with a pending job.
  Tests also prove that the next window can enter its event loop, avoiding an
  accidental application quit when the temporary recovery window disappears.
- Desktop and 1920x1080 Couch failure renders were inspected. Button navigation
  follows the two-column arrangement. These fixtures do not initialize physical
  controllers and do not substitute for maintainer hardware acceptance.
- Development build and all 67 CTest entries passed (65.18 seconds). After
  adjusting arrow navigation to follow the grid, all six startup fixtures passed
  again (1.38 seconds), with no QML warnings/errors in their log.
  `git diff --check` passed.
- The library's export, preview, and confirmation editor is still pending.

### Settings backup editor

- Added Settings → Backup & Restore, backed by asynchronous export, preview,
  and staging. Native file dialogs and a path field are available; Couch path
  entry uses the existing on-screen keyboard.
- Preview shows the reviewed file, record counts, settings values, missing paths,
  and filter name conflicts. Directional navigation scrolls the preview and moves
  into restore actions. Merge/replacement require a second confirmation, with
  Cancel initially focused. Path-based export confirms destination replacement.
- Confirmation names the reviewed archive and stages that exact payload. It
  focuses Close Omakade and leaves the queued message visible until the app closes.
  Source models stay live and unchanged until the next exclusive startup restore.
- Normal libraries enable the manager; synthetic libraries disable it. Dedicated
  backup fixtures instead use an owned temporary database, settings path,
  recovery folder, and sample archive.
- Desktop and Couch application tests export, preview, scroll, cancel a merge,
  confirm replacement, and verify queued state plus close-button focus. Both
  rendered layouts were inspected. The reviewed path is visible even if the
  editable input field changes afterward.
- Development build and all 71 CTest entries passed (18.39 seconds with four
  workers). `git diff --check` passed. BackupEditor produced no QML errors in
  the test log. README and the backup contract now describe the usable workflow.
- The first five backup plan items are implemented. Released-database migration
  evidence and final hardware/maintainer acceptance remain to be completed.

### Released v1.6 database migration

- Exported frozen release commit c91b14e40437a14849f37c188d5762c12655299e
  into a separate build directory and compiled its core with a small schema
  harness. The harness was verified to use the release headers, not candidate
  headers. It runs source constructors with an explicit fresh database path and
  never runs an event loop, scanner refresh, or game launch.
- Generated the actual 20-table release schema with user_version 9. Added the
  reviewable SQL fixture, pinned SHA-256, synthetic personal-data seed, harness,
  and reproduction notes under tests/fixtures/released-v1.6.0.
- The new regression exports the untouched released database, verifies its file
  bytes are unchanged, initializes every candidate source model plus manual and
  unified models, and compares the complete personal-data/artwork snapshot.
- Verified disconnected identities, source/runner mappings, flags, explicit
  links, organization, memberships, activity, and old cover-only artwork survive.
  A pre-migration archive restores through the real recovery coordinator with
  identical personal data and artwork bytes. Original artwork stays on disk.
- Local account identifiers, owned-library rows, and disposable metadata remain
  local and survive replacement, while being excluded from portable data.
- Development build and the core plus backup recovery CTest entries passed
  (6.43 seconds). `git diff --check` passed. The backup plan's final migration
  validation item is now checked. This is database evidence, not hardware or
  game-launch compatibility evidence.

### Linked preferences and GOG controller acceptance fixtures

- Added desktop and Couch control fixtures for choosing a linked manual install,
  reaching Make Default with directional navigation, reopening details, losing
  the default executable, falling back with an explanation, and reconnecting.
  The saved preference survives and no game is launched by these operations.
- Added desktop and Couch GOG Settings fixtures for controller text entry,
  directional access to Add Folder, available/missing-folder messages, settings
  persistence, removal, return focus, and unchanged sentinel game files.
- The GOG removal test found a real delegate-lifetime bug: deleting the row
  destroyed the scope used by its remaining click handler. Moved removal and
  focus restoration into the stable Settings scope. Both controller tests now
  pass without that QML ReferenceError.
- Scaled the GOG path field with Settings text and enlarged the unavailable-default
  explanation for Couch Mode. Inspected six desktop/Couch renders covering GOG
  folders, the saved default, and the missing-default fallback.
- Strengthened the CLI regression to select a nonpreferred Demo installation
  while Lutris is saved as the default, proving explicit targets stay exact.
- Development build and all 81 CTest entries passed (21.44 seconds with four
  workers). `git diff --check` passed. The preference/folder implementation items
  are now checked in the plan. Real launcher and physical-controller acceptance
  remain separate from these synthetic fixtures.

### Next work

1. Reconcile Linear and stale release documents against current implementation.
2. Resolve Prism, Steam Input, and additional emulator ideas with explicit decisions.
3. Audit the remaining release/compatibility requirements and prepare the exact
   candidate plus maintainer checklist, preserving external hardware gates.

No commits, pushes, tags, public comments, releases, or installations performed.

### Tracker and historical documentation reconciliation

- Re-read the live GitHub issue list, 1.6.0 release, Linear project, and
  SBS-1135 release record. The only open GitHub issues are real-library
  validation (#9) and ARM64 packaging/hardware (#13). ARM64 release assets
  exist; hardware evidence remains a separate requirement.
- Updated and fetched back the Linear project overview to identify public
  1.6.0, the unpublished completion scope, and remaining acceptance gates.
  Existing project resource links still need canonical-link cleanup; issue-level
  completion tracking and SBS-1115 wording still need reconciliation.
- Replaced stale future-1.6 priorities in PLAN.md with the active completion
  plan. Marked the old 1.6 review handoff as historical. Updated COMPATIBILITY.md
  to distinguish released 1.6 evidence from the local 81-test candidate.
- No GitHub comments, issue closures, commits, pushes, tags, or releases made.
- Next: issue-level tracking, explicit Prism/Steam Input/emulator decisions,
  and remaining acceptance audit before preparing the exact release candidate.

### Integration scope decisions

- Added INTEGRATION-DECISIONS.md after inspecting the current scanner/launcher,
  GitHub issue inventory, local Prism availability, and upstream Prism/Steam docs.
- Deferred automatic Prism discovery and additional emulator sources with explicit
  reopening criteria. Documented the existing Steam-owned non-Steam shortcut path
  and the exact outstanding physical Steam Input acceptance checklist.
- Added user-facing README instructions for selecting Steam when its compatibility
  or controller settings are wanted. No Steam data was modified or game launched.
- `ctest --preset dev -R core --output-on-failure`: 1/1 passed, 5.62 seconds.
  The initial `^core$` selector matched no tests; the corrected selector above
  ran omakade_core_tests. `git diff --check` passed.
- Steam Input remains unchecked in the completion plan because live controller
  behavior has not been established.

### Release build, requirement audit, and completion tracker

- `cmake --preset release` and `cmake --build --preset release -j4` passed.
  Configure reports optional Vulkan headers unavailable; the build succeeds.
- `ctest --preset release -j4 --output-on-failure`: 81/81 passed, 21.25 seconds.
- Desktop metadata validation passed. AppStream validation succeeded with one
  pedantic notice. `python tests/SbomGeneratorTests.py`: 2 tests passed.
- Installed into the new `build/completion-stage/usr` directory and inspected
  all 11 installed paths: binary, desktop file, SVG icon, AppStream metadata,
  two license files, and five documentation files. Staged metadata validates.
  This is staged-install evidence, not package lifecycle or clean Omarchy evidence.
- Audited manual-entry code and native launch/desktop import regressions against
  section 3. Audited durable artwork ownership, input bounds, independent reset,
  linking and restart regression against section 4. Marked their implementation
  requirements checked; maintainer and real hardware acceptance remain separate.
  Custom images are stored beside the data database, outside downloaded caches.
- Created and fetched back Linear SBS-1136 with all eight acceptance criteria,
  dependency order, and remaining gates. Verified blockedBy relations to SBS-1113
  and SBS-1115. Updated and fetched back SBS-1115 to reflect shipped ARM64 assets
  and outstanding hardware evidence; it remains In Progress.
- Updated privacy/support documentation for manual arguments, portable archives,
  retained local recovery copies, and restore failure reporting.
- Packaging version is still 1.6.0 while the final candidate is prepared. No
  release package has been produced for the completion scope. Local Docker is
  available, including an existing omakade-ci image, for isolated package checks.
- Next: finish resource-link cleanup, choose candidate version and release notes,
  run isolated package/dependency checks, and prepare exact-candidate acceptance.

### 1.7 version and isolated x86_64 package checks

- Prepared 1.7.0 version metadata, local changelog, and README package commands.
  These links remain local until the approved release exists. Added a draft
  exact-candidate acceptance checklist and maintenance scope in docs/1.7-REVIEW.md.
- Versioned Release rebuild and full matrix passed: 81/81, 27.94 seconds.
- Recorded the package source in build/completion-package/source-manifest.json.
  Source archive SHA256: 90fed12634adb711071f74b8805c83045048300dee479788154cd68b783fe944.
- Built the package in a disposable omakade-ci container after updating its
  dependencies. The first unrestricted build was intentionally stopped when
  compiler concurrency caused memory pressure; the successful run used
  CMAKE_BUILD_PARALLEL_LEVEL=2. No host packages were installed or removed.
- x86_64 package build, install, reinstall, removal, reinstall, and two installed
  offscreen smoke tests passed. This run did not test upgrade from 1.6 or physical
  Omarchy/ARM64 hardware. Lifecycle and build logs are in build/completion-package.
- Package SHA256: 609fa364307fcccb0bccb66d3481891f402c61b5b3ad347c2e5faf4c03ac9d35.
- Generated the SPDX SBOM (166 package names). Arch audit reported zero High or
  Critical upgradable findings intersecting those runtime packages. Broader Grype
  scan still pending at this checkpoint.
- Added and verified canonical repository/site resources on the Linear project.
  The Toolport project API is append-only for links; the two legacy links remain
  alongside explicitly labeled current links. The GitHub Pages API confirms the
  current btsouth.github.io/omakade URL.
- Re-read GitHub #9/#13 comments: no new contributor validation report.
- No commit, push, tag, release, upload, or public comment made.

- The first Grype run exited successfully with zero matches but warned that the
  SBOM distribution was unknown. That result is not accepted as full coverage.
  A follow-up scan with explicit `--distro arch:rolling` is running; inspect its
  JSON and warning log before reporting dependency scan completion.

### Upgrade evidence and ARM64 follow-up

- Explicit Arch Grype scan completed with exit 0, zero fixable matches, a valid
  vulnerability database, identified archlinux distribution, and no warnings.
- Downloaded the public 1.6.0 x86_64 package and verified its release checksum:
  c6a3afa57c8ab7bca4d6a0254613abd6c3fb599fd25ded066391a008d2f8a003.
- Installed released 1.6 in a disposable Arch container, populated the frozen
  released schema with synthetic personal data, and exercised installed
  `omakade --benchmark` through normal startup. Upgraded to local 1.7 and repeated
  normal Desktop startup, Couch startup, removal, reinstall, and normal startup.
  Verified unchanged links, organization, collections/membership, and activity at
  every checkpoint. Logs and verifier are in build/completion-package/upgrade*.
- Existing binfmt support successfully ran the pinned ARM64 CI image. Started
  an emulated ARM64 package/lifecycle/regression run with two compiler jobs.
  Container: omakade-completion-arm64-check. This is architecture automation,
  not physical hardware evidence. Logs: build/completion-package/arm64/.

### Combined large-library and offline acceptance

- Added completionWorkflowPersistsAtLibraryScale to the core regression suite.
  It organizes 1,000 games, links two installations, sets a preferred installation,
  stores all three artwork slots, saves a combined filter, exports/restores the
  archive, and verifies all 999 visible linked identities after restart and
  restore. It checks retained organization and repeated filtered random picks.
- The focused test passed in 1.956 seconds. All 81 Release CTest entries then
  passed inside `unshare --user --map-root-user --net` in 43.22 seconds. This
  gives network-isolated evidence for the actual automated editor/navigation,
  backup/recovery, rendering, startup, and core flows. It is synthetic-library
  evidence, not physical controller or game compatibility acceptance.
- The ARM64 run remains active under container omakade-completion-arm64-check.
  Copied the updated CoreTests.cpp into its test source before test compilation,
  so the eventual ARM core suite also includes the new large-library regression.
  The package source archive is unchanged; this supplemental test-only change
  must be included when preparing the final commit-bound source archive.

### Source candidate checkpoint

- Preparing a local candidate commit after current x86_64 Release/offline checks
  passed. The exact revision and final artifacts will be recorded in
  build/completion-package/CANDIDATE.json, outside the commit to avoid embedding
  a self-referential revision.
- ARM64 emulated validation remains active; it does not authorize publication.
- RELEASING.md now requires inspecting scan distribution/database diagnostics
  rather than treating an unidentified distribution as a successful scan.
