# Publishing an update

`update.json` on the `main` branch is the public update feed. No credentials are shipped in the plugin. Checks use HTTPS on an owned background worker, retain normal certificate validation, and cache the feed in `%LOCALAPPDATA%/MXB-FPV` for six hours. Manual checks bypass the cache.

The manifest has `schema: 1` and two channels: `stable` and `preview`. Either channel may be `null`. A release entry contains:

```json
{
  "version": "0.6",
  "notes": "Short release notes for the in-game Updates page.",
  "game_sha256": ["supported executable SHA-256"],
  "installer": { "sha256": "installer SHA-256", "bytes": 123456 }
}
```

Versions have two or three numeric components, such as `0.6` or `0.6.1`. The checker picks the newest allowed channel. It compares numeric versions, rather than treating them as decimal numbers. URLs are constructed from the pinned repository, version tag and expected installer name; the manifest cannot supply an arbitrary download URL.

For a new release:

Use PowerShell 7 for the packaging script, plus Visual Studio 2022 C++ Build Tools and a Windows SDK for compilation.

1. Update `mod/src/version.h`, visible version labels, README and release notes. A changed game binary also requires revalidating the native adapters; changing a hash alone is insufficient.
2. Run `./mod/build.ps1 -SkipTests`, then `./Package.ps1 -Channel preview`. This creates the installer, tester ZIP, source ZIP, checksums and a **proposed** manifest in `out/<version>/update.json`.
3. After the user's review/authorization, commit the source/docs and push `main`. Create a **draft** GitHub release tagged `v<version>` at that commit. Upload the assets from `out/<version>` except `update.json`.
4. Check that uploaded asset sizes and SHA-256 digests match `out/<version>/SHA256SUMS.txt`. Publish the release; mark preview versions as prereleases.
5. Only after assets are public, replace the root `update.json` with `out/<version>/update.json`, commit it and push `main`. This prevents the updater offering an unavailable installer.

Use `-Channel stable` only for a release you intend to call stable. Preview releases should be tested with multiple clients before promotion. Preserve the other channel when updating a manifest.

The downloader accepts at most 128 MiB, requires the exact expected size/hash and matching game hash, stages the verified file in Downloads, and never runs it. Setup refuses to replace a loaded plugin while the game is running, preserves the INI and keeps the previous recognized plugin for rollback.

This repository contains only the FPV source, its required libraries/assets and documentation. Game executables, runtime dumps, research tools and the separate bike-switch mod are excluded.
