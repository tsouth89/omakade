# Backup format and implementation notes

Status: archive codec, read-only snapshot exporter, internal database importer,
core-settings restore method, recovery journal, and asynchronous preview/export
service implemented. Startup recovery and Settings export/preview/confirmation
controls are integrated. Released-database migration is covered by a fixture
generated from the frozen v1.6.0 core. Release and maintainer acceptance checks
remain part of the completion plan.

## Version 1 archive

A ZIP archive contains `manifest.json` and referenced custom artwork. The
manifest has exactly these fields:

- `format`: `omakade-backup`
- `version`: `1`
- `createdAt`: ISO timestamp
- `library`: allowlisted personal-data tables, represented as arrays of records
- `settings`: allowlisted core application preferences
- `artwork`: the exact inventory of artwork archive paths

The field lists are defined by `BackupArchive::tableColumns()` and
`BackupArchive::settingNames()`. Unknown tables, fields, settings, or versions
are rejected. The reader validates into memory and does not extract files.

Artwork names are `artwork/<sha256>.<png|jpg|webp>`. The digest and image format
must match the decoded content. Every file must be referenced by an artwork
record, and every nonempty artwork reference must have a matching file.
Transparency and the selected image bytes are retained.

Limits: 16 MiB manifest, 32 MiB per image, 512 MiB total decoded contents,
4,096 artwork files, and 100,000 total personal records. Saved filters retain
the application limit of 500 entries. Images are limited to 16,384 pixels per
dimension and 64 megapixels. ZIP entries must be unencrypted and use Store or
Deflate compression. Unexpected paths and non-regular Unix file entries are
rejected. Reads fail without replacing the caller's previous payload. Writes
use a temporary archive and an atomic file replacement with owner-only access.

## Personal data

The archive includes favorites and hidden state, completion states, tags,
collections, explicit links, preferred installations, Omakade launch activity,
manual entries, saved filters, and custom artwork. It does not contain game
files, launcher databases, achievement caches, downloaded artwork caches,
Omakade account-service identifiers, API credentials, or automatic Sunshine publishing settings.
Manual launch arguments and custom image bytes remain part of the user's data.

Legacy cached source flags are converted to `user_game_flags`. Non-null newer
overrides take precedence over the corresponding legacy field. Conversion uses
the identities actually exposed by each source model:

| Cached table | Source | App ID | Runner |
| --- | --- | --- | --- |
| games | Steam | app_id | empty |
| lutris_games | Lutris | id | empty |
| heroic_games | GOG when gog-direct, otherwise Heroic | app_id | runner |
| faugus_games | Faugus | game_id | empty |
| retroarch_games | RetroArch | game_id | empty |
| pcsx2_games | PCSX2 | path: followed by path | serial |
| ryujinx_games | Ryujinx | game_id | flatpak_app_id |
| battlenet_games | Battle.net | game_id | runner |
| manual_games | Manual | id | empty |

All cached personal choices are included, including undiscovered or disconnected
entries. A snapshot uses a separate read-only SQLite transaction. Legacy cover
records gain empty hero/logo slots in the archive. Missing or invalid custom
artwork fails export with a repair/reset message instead of silently discarding
that choice. Missing native game files do not invalidate a structurally valid
manual entry, and reading an archive never launches one.

## Database import and settings behavior

`BackupDatabase::restore` is an internal primitive for use while no source models
are live. BackupRecovery prepares a recovery backup before calling it. The app
must hold its startup gate until the recovery operation succeeds.

Artwork is staged as validated, immutable, content-addressed files under the
owned artwork folder before database references change. Existing artwork is not
deleted. An unsuccessful database restore can leave unreferenced staged artwork,
which is safe to reuse during recovery. Database schema preparation and all
personal-record mutations occur in one SQLite transaction.

Merge imports archive values for matching identities and keeps other personal
records. Collections and memberships are added without removing unrelated
memberships. Incoming linked-group definitions take precedence for their members;
other groups retain their remaining members when at least two remain, with one
primary and a valid preference. An imported saved filter with a conflicting name
gets a deterministic `restored` suffix. Replaying the same import is idempotent.

Replacement clears personal tables and resets cached legacy favorite/hidden
flags before importing the archive. It leaves cached game records and game files
intact. Manual entries with missing executable paths stay available for repair.

`AppSettings::applyBackupSettings` applies the core allowlist in one file save.
Merge retains unspecified core preferences; replacement uses their defaults.
Account-service identifiers and Sunshine publishing choices remain unchanged in
both modes. Failed saves restore the prior in-memory core settings. This method
alone does not make a database-plus-settings restore atomic; the coordinator
must provide that recovery boundary.

## Process interruption recovery

`BackupRecovery::stage` saves the exact validated payload in a new private job
folder and atomically records a queued request. It leaves the live database and
settings unchanged. A second request cannot replace a pending one. Completed job
folders remain available as recovery history.

`resume` and `undo` require exclusive app ownership before source models or
services start. They serialize recovery operations with QLockFile. On first
resume, the coordinator snapshots the latest personal library state and saves
an exact private copy of the settings file. That raw copy can contain local
account settings and is never included in the portable archive. All recovery
files use owner-only access; job folders use owner-only traversal.

The journal advances to prepared only after recovery files have been written.
Database import runs before the core settings write. Any failure leaves the
journal pending, and the caller must keep the startup gate closed. A subsequent
resume replays the same idempotent import. Archive digests detect replacement of
the staged or recovery ZIP, even if the replacement is a valid backup.

Undo records its direction before changing data, replaces personal database
state from the pre-restore archive, and restores the exact original settings
bytes (or the original absence of that file). Normal resume continues an
interrupted undo. Canceling a queued request changes no library data. Once a
restore has completed, its recovery archive must be previewed as a new request;
undo cannot silently overwrite later library edits.

Tests terminate child processes with `_Exit` after preparation, after database
commit, after settings save, and after completion marking in merge and replace
modes. Separate cases interrupt both undo writes, fail a settings save, corrupt
recovery data, reject a changed incoming archive and invalid journal path,
restore an initially absent settings file, and retain previous recovery jobs.
These tests prove process interruption recovery, not sudden power-loss behavior.
The normal startup path checks recovery after acquiring SingleInstance and before
constructing source models, preferences, or account services. Pending work opens
an isolated RestoreStartup window driven by BackupStartup on a worker thread.
Success closes that temporary window and continues into the library. Failure
keeps startup gated with Retry, Undo Restore, Recovery Folder, and Close actions.
Closing while a write is active is disabled; process interruption is handled by
the journal on the next launch. Synthetic demo/render/navigation/stress libraries
skip real recovery entirely.

Six application fixtures cover successful startup, failure and retry, failure and
undo, closing with work pending, and rendered desktop/Couch recovery screens.
They use owned temporary state, do not initialize hardware controllers, and verify
that a subsequent window can enter its event loop after recovery. Keyboard and
controller-direction fixtures follow the two-column button grid. Physical
controller acceptance remains separate.

## Preview and export service

`BackupManager` runs archive reads, snapshot capture, writes, and restore staging
on a worker thread. UI state exposes busy, result messages, and a validated
preview. Preview reports imported/current/matching record counts, artwork count,
core settings values, saved-filter name conflicts, and missing manual executable,
working-directory, and configured GOG paths. Path details are capped at 100 while
the total count remains available. Matching counts describe stable record IDs;
the merge explanation separately covers incoming linked-group precedence.

Confirmation stages the immutable in-memory payload that produced the preview.
Changing the selected external archive afterward cannot change what is restored.
Reading an invalid archive discards any earlier preview. Busy operations cannot
be confirmed or replaced. The availability flag lets the application disable
backup operations in demo and test views. The normal app enables the manager only
for its real library; dedicated editor fixtures use their own temporary database,
settings, recovery folder, and sample archive.

Export requires a local `.omakade-backup` destination and rejects application
state targets and owned artwork/recovery folders. Portable exports continue to
exclude account identifiers. Neither preview nor confirmation changes the live
database or settings; successful confirmation emits a queued signal for the UI
close/reopen flow. The startup gate applies the queued job exclusively.

## Library editor

Settings opens BackupEditor in both desktop and Couch Mode. Native file dialogs
and an editable local path support export and preview. Path-based export confirms
replacement of an existing destination. Couch path entry uses the existing
on-screen keyboard. The scrollable read-only preview names the reviewed file,
shows personal-data and preference details, and supports directional navigation
to the merge/replacement actions.

Both restore modes open a separate confirmation with Cancel initially focused.
Confirmation stages the validated data and focuses Close Omakade. Reopening the
app applies the queued restore through the exclusive startup gate. The editor
keeps the queued message visible until the app closes. No live models are
reloaded or mutated by confirmation.

Application tests export an archive, preview it, scroll with a controller,
cancel a merge, confirm replacement, and verify queued recovery plus close-button
focus. Desktop and Couch preview renders were inspected. Real-controller and
maintainer acceptance remain part of the completion plan.

## Released-database migration evidence

The released v1.6.0 core at `c91b14e40437a14849f37c188d5762c12655299e` was
built separately and used to initialize a fresh SQLite database with all source
model constructors. Its 20-table schema and user_version 9 are stored in
`tests/fixtures/released-v1.6.0/schema.sql`, with a pinned digest and reproduction
instructions. Synthetic personal data covers every source, direct GOG, a missing
Steam installation, linked identities, organization, collections, launch counts,
and the old cover-only artwork table.

The regression exports before candidate constructors touch the database, checks
that the original file is unchanged, then runs every current source model plus
manual and unified models. The personal-data snapshot remains identical. Restoring
the pre-migration archive through BackupRecovery again preserves personal data
and image bytes, retains the original custom-art file, leaves account/cache rows
local, and keeps them out of portable personal data. This validates database
migration, not game launching or hardware compatibility.
