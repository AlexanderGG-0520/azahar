# YW fork prerelease procedure

The YW fork publishes **Windows installer and portable ZIP, macOS universal ZIP,
and a signed vanilla Android APK**. Linux users build from the source tag.
The release workflow is `.github/workflows/yw-release.yml`.

## Preconditions

1. Merge the release workflow and changes intended for release into
   `integration/yw`. Check that required CI and maintainers' local runtime
   verification are acceptable. A successful compile is not evidence that HOME
   transitions work on an untested OS.
2. Set up GitHub Actions secrets **on this repository** for Android signing:
   `ANDROID_KEYSTORE_B64` (base64 of the binary keystore file),
   `ANDROID_KEY_ALIAS`, and `ANDROID_KEYSTORE_PASS`. The keystore and its
   private key must be backed up securely and reused across Android updates.
   Do not commit the keystore, passwords, or a base64 value to Git.
3. Prepare `docs/releases/yw-vX.Y.Z.md` with verified changes and clear platform
   testing caveats. The release workflow **requires** this file.
4. For macOS, the current universal build is ad-hoc signed (not Apple Developer
   ID signed or notarized); communicate this accurately. Android signing uses
   the configured fork keystore, which need not match the upstream Azahar key.

## Triggering a release

Use a fresh release branch directly from the verified integration tip (example
for v0.2.2; fish shell):

```fish
git fetch origin
git switch integration/yw
git pull --ff-only
git switch -c release/yw-v0.2.2
git push -u origin release/yw-v0.2.2
```

A **subsequent commit** creating a marker file under
`.github/release-trigger/` triggers the release workflow:

```fish
mkdir -p .github/release-trigger
printf 'Release yw-v0.2.2\n' > .github/release-trigger/yw-v0.2.2
git add .github/release-trigger/yw-v0.2.2
git commit -m 'release: trigger yw-v0.2.2'
git push
```

A manual `workflow_dispatch` run is also supported, but it must select the
`release/yw-vX.Y.Z` branch. Other branches fail version validation. The
manual trigger defaults to **`dry_run=true`**, which compiles each target and
verifies/archives the complete release payload **without publishing**. Only
explicit `dry_run=false` (or the marker-file push trigger) publishes it.

The parallel Windows, macOS, and Android jobs must **all succeed** before
the `verify-assets` job runs. It validates the four expected payloads,
produces `SHA256SUMS.txt`, and preserves a verified artifact that can be
inspected even in dry runs. Only then may the `publish` job create a
**prerelease**. If Android signing secrets are missing, the release fails
closed instead of publishing an APK signed with a debug key. A failed or
incomplete job means **no new release**.

## After the workflow

- Verify all four assets are present in the GitHub Release, not just in
  workflow artifacts.
- Verify checksums match the published assets.
- Test Windows setup and ZIP extraction, macOS launch/Gatekeeper behavior,
  and Android install/update using the **same signing key**.
- Keep build success and game-level HOME/StreetPass behavior labeled separately
  in release notes.
- Never bundle Nintendo firmware, HOME system content, games or copyrighted ROMs.

Rerunning the publish job updates existing assets on the same release tag using
`--clobber`; release notes are not overwritten automatically by a rerun.
