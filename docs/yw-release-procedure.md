# YW fork prerelease procedure

The YW fork currently publishes **Windows installer and portable ZIP plus a macOS
universal ZIP**. Linux users build from the source tag. **Android APK builds
and signing are deferred** until a future release.
The release workflow is `.github/workflows/yw-release.yml`.

## Preconditions

1. Merge the release workflow and changes intended for release into
   `integration/yw`. Check that required CI and maintainers' local runtime
   verification are acceptable. A successful compile is not evidence that HOME
   transitions work on an untested OS.
2. Prepare `docs/releases/yw-vX.Y.Z.md` with verified changes and clear
   platform testing caveats. The release workflow **requires** this file.
3. For macOS, the current universal build is ad-hoc signed (not Apple Developer
   ID signed or notarized); communicate this accurately.
4. Android is intentionally **not built or packaged** by this workflow.
   No Android signing secrets are needed. When Android distribution is resumed,
   establish a long-lived fork signing key and reintroduce the APK checks.

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

The parallel Windows and macOS jobs must **both succeed** before the
`verify-assets` job runs. It validates the **three** expected desktop
payloads, produces `SHA256SUMS.txt`, and preserves a verified artifact
that can be inspected even in dry runs. Only then may the `publish` job
create a **prerelease**. A failed or incomplete job means **no new release**.
Android is not a prerequisite because no APK is built in this release.

## After the workflow

- Verify all three desktop assets are present in the GitHub Release, not just in
  workflow artifacts.
- Verify checksums match the published assets.
- Test Windows setup and ZIP extraction, and macOS launch/Gatekeeper behavior.
- Keep build success and game-level HOME/StreetPass behavior labeled separately
  in release notes.
- Never bundle Nintendo firmware, HOME system content, games or copyrighted ROMs.

Rerunning the publish job updates existing assets on the same release tag using
`--clobber`; release notes are not overwritten automatically by a rerun.
