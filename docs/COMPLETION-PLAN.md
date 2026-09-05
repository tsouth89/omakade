# Omakade completion plan

Created September 5, 2026. Baseline public release: 1.6.0.

Implementation worktree: `/home/bts/Projects/omakade-completion`, branch
`codex/completion`, based on released commit `c91b14e`. The original
`/home/bts/Projects/steam-launcher` checkout and its uncommitted changes are
preserved separately. Execution evidence is in the implementation worktree's
`docs/COMPLETION-PROGRESS.md`.

## Outcome

Finish the library conveniences below, validate the supported launch paths,
reconcile the project records, and prepare one final release candidate before
moving to maintenance. Choose the release version when the candidate scope is
settled. Implementation alone does not authorize publication.

## Working rules

- Preserve existing uncommitted work. Establish its ownership and relationship
  to the released commit before integrating or separating it.
- Use the released source as the baseline, not stale candidate documentation.
- Keep core functionality local and usable offline. Optional services must not
  delay startup or block library use.
- Keep accounts, installation, updates, compatibility tools, saves, and stores
  with their existing launchers.
- Make new controls work with mouse, keyboard, and controller. Use desktop
  file pickers for file setup where necessary and explain that boundary.
- Test changes against user-visible failure modes, including migration and
  preservation of personal data. Do not substitute automated rendering for
  maintainer visual and controller acceptance.
- Access Linear through Toolport. Inspect exact public text before submitting
  changes and verify the result. Do not post comments or solicit reports without
  explicit authorization to send those messages.

Linear completion tracker: [SBS-1136](https://linear.app/southboundsoftware/issue/SBS-1136/complete-library-conveniences-and-prepare-maintenance-release).

## 1. Establish the baseline and track the work

- [x] Compare the current checkout and uncommitted model/test changes with
  v1.6.0. Preserve and document what is retained or kept separate.
- [x] Record the implementation baseline and run the relevant existing checks.
- [x] Represent the work below in Linear with acceptance criteria, dependencies,
  and a clear distinction between implementation and external validation.
- [ ] Correct Linear's stale 1.4.0 overview and old account links. Reconcile
  PLAN.md, COMPATIBILITY.md, and the historical 1.6 review handoff with the
  verified release record. Preserve historical evidence as dated history.

## 2. Persistent library preferences and folders

### Preferred installation

- [x] Persist an explicit preferred installation for each linked game.
- [x] Use it consistently in details, direct Play, command-line launching, and
  Sunshine exports where the request identifies a linked game rather than an
  explicit installation. Explicit source requests retain their exact target.
- [x] Explain an unavailable preference and provide a usable fallback without
  erasing the preference during a disconnected drive or failed scan.
- [x] Preserve the preference across restart, rescan, and link changes; handle
  unlinking and removal without dangling references.

### Configurable GOG folders

- [x] Add, inspect, and remove extra direct GOG library roots in Settings.
- [x] Preserve standard discovery and define precedence with the existing
  OMAKADE_GOG_LIBRARY_PATHS override.
- [x] Deduplicate overlapping roots, handle missing drives without dropping
  personal state, and report inaccessible paths clearly.
- [x] Removing a configured root never deletes game files.

## 3. Manual games

- [x] Add an explicit manual source for user-selected native executables and
  desktop entries, with title, arguments, working directory, and artwork.
- [x] Parse supported desktop-entry fields correctly. Store executable and
  argument boundaries rather than evaluating arbitrary shell text.
- [x] Support edit, launch, and removal of the Omakade entry. Removal never
  deletes or uninstalls the underlying game.
- [x] Include manual games in search, filters, organization, linked games,
  activity, controller flows, and launch entry points where applicable.
- [x] Handle missing targets, moved files, spaces, and malformed entries with
  actionable messages. Do not silently import general applications.
- [x] Keep Windows runner setup delegated to existing launchers.

## 4. Artwork and organization

### Artwork customization

- [x] Extend user-selected artwork to covers, heroes, and logos.
- [x] Store durable copies under Omakade data, validate supported image content,
  and use appropriate size limits and aspect handling.
- [x] Reset each artwork type independently to its normal provider fallback.
- [x] Preserve selections through restart, rescan, linking, and cache clearing.

### Bulk organization

- [x] Add an explicit multi-selection mode with visible selection and count.
- [x] Apply favorite, hidden, tag, collection, and completion-state changes to
  the selected games. Mixed values must have unambiguous behavior.
- [x] Keep selection tied to stable identities through sorting and refreshing.
  Clearly define select-all as the current filtered results.
- [x] Apply database changes atomically and preserve predictable focus when
  an operation removes games from the current filter.
- [x] Make selection, action choice, cancellation, and exit usable with a
  controller and keyboard as well as a mouse.

### Saved filters

- [x] Save, rename, apply, and delete named combinations of supported search,
  source, status, collection, tag, and sort settings.
- [x] Preserve existing collections as collections; saved filters are dynamic
  queries rather than copied membership lists.
- [x] Handle renamed or removed filter dependencies clearly and preserve the
  current game where it still matches.

### Pick something to play

- [x] Choose a random eligible game from the current filtered library.
- [x] Show the selection for inspection and confirmation instead of launching
  a game immediately. Offer another pick and a clear empty state.
- [x] Respect hidden games, linked identities, installation availability, and
  explicit filters. Avoid immediate repeats when alternatives exist.

## 5. Backup and restore

Implement after the new persistent preferences, manual entries, artwork, and
saved filters have stable schemas.

- [x] Export a versioned local archive of personal library data: favorites,
  hidden state, collections, tags, completion states, links, preferences,
  manual entries, saved filters, custom artwork, and supported settings.
- [x] Exclude credentials, game files, launcher databases, and disposable caches.
- [x] Provide a preview of archive contents and clearly explain merge or
  replacement semantics before applying a restore.
- [x] Validate archive paths, sizes, formats, and schema compatibility before
  writing. Make restore transactional and keep a recoverable pre-restore copy.
- [x] Reconcile stable source identities and missing paths on another machine
  without losing user choices or launching imported entries automatically.
- [x] Verify round trips, interrupted restores, invalid archives, and migration
  from the released database. Accounts require connection on another machine;
  existing local account settings remain unchanged.

## 6. Resolve older feature ideas

These decisions are part of completing the roadmap. They do not silently expand
this release into unrestricted new source development. Decisions and remaining
Steam Input hardware acceptance are recorded in
[INTEGRATION-DECISIONS.md](INTEGRATION-DECISIONS.md).

- [x] Assess Prism Launcher demand, discovery and launch contracts, and access
  to a real configured installation. Record a bounded implementation proposal
  or an explicit deferral for maintainer review.
- [ ] Assess Steam Input integration separately from existing Steam shortcut
  import. Identify the exact user journey, ownership of shortcuts, and whether
  documentation or a product change is needed. Validate the supported path.
- [x] Inventory outstanding emulator requests. Existing PCSX2, Ryujinx, and
  RetroArch support is already shipped; additional integrations require a named
  request and their own compatibility commitment. Record the disposition.

## 7. Compatibility and final acceptance

- [ ] Reconcile GitHub #9 / SBS-1113 with actual versioned evidence for native
  and Flatpak launchers, clean install/upgrade, direct native and Windows GOG,
  and supported emulator and Battle.net paths. Separate fixtures from real use.
- [ ] Reconcile GitHub #13 / SBS-1115: ARM64 packages are shipped; obtain or
  await the exact-version M1/Asahi report. Do not claim hardware validation from CI.
- [ ] Test the complete new workflows in desktop and Couch Mode, offline and
  after restart, with a large library and linked installations.
- [ ] Run relevant core, migration, launch, controller, rendering, packaging,
  metadata, and dependency checks. Record actual commands and results.
- [ ] Verify common display sizes and scaling, contrast, reduced motion, focus,
  and missing/unavailable sources. Fix regressions before requesting acceptance.
- [ ] Prepare the exact candidate commit, check results, known limitations, and
  a short manual checklist for maintainer testing.

## 8. Release and maintenance handoff

- [ ] Update feature, support, privacy, compatibility, and release documentation
  to describe the implemented behavior accurately.
- [ ] Reconcile Linear and GitHub statuses without closing unresolved external
  validation as completed. Record each dependency and remaining action.
- [ ] Obtain maintainer testing and explicit publication approval for the exact
  candidate before pushing commits, creating or pushing tags, publishing a
  release, or uploading assets.
- [ ] After approval, complete the authorized release procedure and verify the
  published packages, checksums, provenance, and user-facing release information.
- [x] Record maintenance scope: bug fixes, source compatibility fixes, security
  and dependency updates. New product features require a new decision.

## Completion criteria

All eight convenience features are implemented and verified; older source and
Steam Input ideas have explicit decisions; current documentation and trackers
agree; and the maintainer has an exact, tested candidate to review. Publication
and external hardware reports remain separate gates and must be reported as
pending until their evidence or authorization exists. Never mark the overall
goal complete while required implementation or acceptance work remains.
