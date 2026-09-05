# Omakade privacy

Omakade is local-first. It has no analytics, advertising, telemetry, account
system, or required online service.

## Local data

Omakade reads Steam library manifests, artwork caches, playtime, recent-play
state, and achievement caches. It also reads installed-game manifests and
cached artwork from Lutris and Heroic, Heroic's sideloaded game list and play
timestamps, configured playlists, thumbnails, and runtime logs from RetroArch,
and Battle.net Agent product databases plus last-played stamps inside Wine,
Proton, and Bottles prefixes. It never writes into source launcher directories.

Omakade retains:

- Library, source records, favorites, hidden state, and achievements in
  `$XDG_DATA_HOME/omakade/library.sqlite3`
- User-created links and preferred installations in the same database
- Manual game titles, executable paths, arguments, working directories, and
  saved filter queries in the same database
- Completion states, tags, collections, and collection memberships in the same
  database
- Owned Steam App IDs, titles, and account playtime after an explicit library
  sync in the same database
- Steam ID, RetroAchievements username, public IGDB client ID, cache limit, and
  reduced-motion preference in
  `$XDG_CONFIG_HOME/omakade/config.toml`
- Downloaded covers and achievement icons in `$XDG_CACHE_HOME/omakade/`
- Copies of covers, heroes, and logos selected by the user in
  `$XDG_DATA_HOME/omakade/artwork/`
- Configured GOG folders and desktop/Couch Mode preferences in the settings file
- Private restore jobs and recovery copies in
  `$XDG_DATA_HOME/omakade/restore-recovery/`

The Steam ID is an account identifier, not a credential. A Steam Web API key
is stored only through the desktop Secret Service under
`io.github.tsouth89.Omakade.Steam`. Older preview keys stored as
`io.github.omakade.Steam` remain readable. The key is never written to Omakade's config, database,
logs, or process arguments.

A RetroAchievements Web API key is stored only through the desktop Secret
Service under `io.github.tsouth89.Omakade.RetroAchievements`. It is never
written to Omakade's config, database, logs, or process arguments.

Optional IGDB game insights require a Twitch developer client ID and client
secret supplied by the user. The public client ID is stored in Omakade's config.
The client secret is stored through Secret Service as
`io.github.tsouth89.Omakade.IGDB` and is never written to config, the database,
logs, or process arguments. Omakade sends these credentials to Twitch only to
obtain an app access token, then sends the token and client ID to IGDB.

## Backup and restore

Export creates a local archive at the path you choose. It includes personal
library choices, manual launch details, saved filters, custom artwork, and
supported preferences. It excludes API credentials, Omakade account-service
identifiers, game files, launcher databases, and downloaded caches. Paths and
manual arguments can contain personal information; an export is not encrypted.
Omakade does not upload it.

Restore keeps the incoming archive and a pre-restore recovery archive locally.
It also keeps an exact copy of the local settings file for interrupted-restore
recovery, so the private recovery folder can contain local account identifiers
that portable exports omit. Recovery files have owner-only access. Completed
recovery jobs are retained; they are not automatically deleted after success.
Restoring does not reconnect accounts or launch imported entries.

## Network requests

Omakade may request missing covers and achievement icons from Steam's public
HTTPS artwork hosts, and missing Battle.net covers and banners from Lutris's
public game-art URLs. Responses are size-limited and the artwork cache is
bounded by the configured limit.

Steam Web API requests occur only after the user stores a key. Omakade refreshes
stale achievement data when Steam game details open or when the user selects
Refresh Steam. It requests player achievements, the game's achievement schema,
and global rarity from Valve's documented HTTPS endpoints. Failed requests do
not remove cached data.

Owned-library requests occur only when the user selects Sync Owned Steam
Library. Omakade requests the public game list and playtime for the configured
Steam ID, then caches it locally. A failed or private-profile response does not
replace the previous cache. Steam remains responsible for installation.

IGDB requests occur only after the user supplies their own Twitch developer
credentials. Omakade maps a Steam App ID to an IGDB game, then requests IGDB's
external critic aggregate and game-length estimates. Responses are cached in
the local library database for offline use and refreshed after 30 days.

RetroAchievements requests occur only after the user supplies a username and
Web API key. Omakade downloads supported game hashes and matches ROM hashes
locally. It sends the matched game identifier and configured username to
RetroAchievements to retrieve progress. Responses are cached in the local
library database for offline use.

## Removal

The settings panel can clear downloaded achievement art and remove Steam,
RetroAchievements, or IGDB credentials from Secret Service. Removing Omakade does not remove its XDG
data by default, so users can preserve settings across reinstallations.
Resetting a custom artwork slot removes its unused Omakade copy and restores
the source-provided artwork. It does not change the original selected image.
