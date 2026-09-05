# Releasing Omakade

Never push the release commit or tag until the maintainer has tested the exact
candidate locally and explicitly approved publication. Before requesting that
approval, record the candidate commit, checks run, known limitations, and a
short manual test checklist. A critical hotfix may bypass local maintainer
testing only when the maintainer explicitly authorizes that specific release.

1. Update the version in `CMakeLists.txt`, AppStream metadata, the changelog,
   README package commands, and public feature descriptions.
2. Run the release configure, build, and tests:

   ```bash
   cmake --preset release
   cmake --build --preset release
   ctest --preset release
   ```

3. Validate the desktop and AppStream files. Inspect dependency scan diagnostics:
   an unknown-distribution warning is not proof of a clean package. For local
   x86_64 Arch SBOM scans, use `grype sbom:PACKAGE.spdx.json --distro arch:rolling
   --only-fixed --fail-on high` and verify a valid vulnerability database. Keep
   ARM64 distribution coverage and hardware results explicit.
4. Install into an empty staging directory and inspect every installed file.
   Render visual fixtures without opening a desktop window when needed:

   ```bash
   QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
     ./build/release/omakade --render-screenshot=/tmp/omakade.png \
     --render-size=1380x880
   ```

   Add `--render-overlay=settings` or `--render-overlay=picker` to capture those
   overlays as well.
5. Confirm CI passes on both x86_64 and aarch64. Run the Release workflow
   manually from the candidate branch to build package artifacts without
   publishing them or their attestations. Give the maintainer the exact
   candidate and checklist, then wait for local test results and explicit
   publication approval. For an
   aarch64 release, also have the exact package tested on supported ARM64
   hardware.
6. Tag the approved commit as `vX.Y.Z` and push the tag. Do not push the
   version update to `main` yet.
7. Confirm the Release workflow builds x86_64 and aarch64 packages, installs,
   launches, reinstalls, removes, and reinstalls each package, scans their Arch
   runtime dependencies, and publishes SPDX SBOMs, SHA-256 checksums, and signed
   provenance before creating the GitHub release.
8. Download the public assets, verify their checksums and provenance, then push
   the reviewed commit to `main`. This prevents README install links from going
   live before their release assets.
9. Install, upgrade, remove, and reinstall the published package in a disposable
   Omarchy environment.

Do not publish a package while the source URL or checksum is a placeholder.
