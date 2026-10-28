# YW fork prerelease procedure

The existing `.github/workflows/yw-release.yml` publishes Windows MSYS2 installer/portable ZIP, macOS universal ZIP, and a privately signed Vanilla Android APK. It also validates the `azahar-yw-git` Arch VCS package without submitting it to AUR. Linux desktop users can build from source or use the AUR package once it is published.

## Android signing setup

Create and retain one long-lived Android release keystore outside the repository. Store its base64-encoded keystore and credentials as repository Actions secrets:

- `ANDROID_KEYSTORE_B64`
- `ANDROID_KEY_ALIAS`
- `ANDROID_KEYSTORE_PASS`

The workflow decodes the keystore into a temporary runner file and removes it on exit. A publishable release fails closed if any secret is missing or if the resulting APK is debug-signed. Do not regenerate or replace this key after distribution begins: Android will reject updates signed by another certificate. Dry-run APKs may use the Android debug key and receive a `-debug.apk` suffix; they are validation artifacts and cannot update the signed release app.

Vanilla release APKs use package ID `io.github.alexandergg0520.azaharyw`. Their release `versionName` matches the `yw-vX.Y.Z` tag plus the `-vanilla` flavor suffix. `versionCode` is encoded from release semver (`major * 1,000,000 + minor * 1,000 + patch`) for monotonic release ordering; minor and patch must each be below 1000. Development APKs without a release tag use the existing time-based version code and a `yw-dev-<sha>` version name.

## Release branch and dry-run

Start a release from the verified `integration/yw` tip:

```sh
git fetch origin
git switch integration/yw
git pull --ff-only
git switch -c release/yw-vX.Y.Z
git push -u origin release/yw-vX.Y.Z
```

Create `docs/releases/yw-vX.Y.Z.md` with accurate release and platform-testing notes. The workflow validates the notes together with the four release assets before publishing.

For manual execution, choose the release branch, select `build_target: all`, and leave `dry_run: true` first. This builds and verifies all platforms without publishing. An actual manual publish requires `build_target: all` and an explicit `dry_run: false`; it is accepted only on a `release/yw-vX.Y.Z` branch and requires the signing secrets above. Alternatively, the existing Release Trigger Marker on a release branch triggers the all-target publish path. Marker-only commits tag their parent source commit.

`integration/yw` accepts only Android validation: an integration push touching build inputs runs an Android-only, non-publishing build; a manual integration run must select `build_target: android` and `dry_run: true`. The workflow rejects other branches and any Android-only publish attempt. No development branch push can create or update a GitHub Release.

GitHub permits `workflow_dispatch` only when that workflow file exists on the repository's default branch. The repository default is currently `master`, where `yw-release.yml` is absent. Therefore the Run workflow UI/API will not be available until this workflow definition is merged or otherwise added to `master`. After that, select `integration/yw` in the branch selector for an Android dry-run. Before then, merging this change into `integration/yw` enables the path-limited integration push validation without adding another workflow.

## Jobs and publication gate

- `build_target: all` runs Windows, macOS, Android, and AUR validation.
- `build_target: android` runs only the Android APK job and uploads the APK as a GitHub Actions Artifact. It skips desktop and AUR jobs and can never publish.
- `verify-assets` waits for Windows, macOS, Android, and AUR validation. It verifies the installer, portable ZIP, Universal ZIP, arm64 APK structure/package/version/signature, and release notes; then it writes a `SHA256SUMS.txt` covering all four distributable files.
- `publish` requires the explicit release gate and the successful verified payload. Any missing artifact or failed validation prevents publication.

The canonical release APK is `azahar-yw-vX.Y.Z-android-arm64.apk`. The release is a GitHub prerelease; the same APK is attached directly as a release asset so Obtainium can discover it. Android-only dry-runs and integration builds appear only as Actions Artifacts.

## Obtainium

Install Obtainium from its [official GitHub Releases](https://github.com/ImranR98/Obtainium/releases). Add `https://github.com/AlexanderGG-0520/azahar` as a GitHub source, enable **Include prereleases**, and set **Filter APKs by Regular Expression** to:

```regex
^azahar-yw-v[0-9]+\.[0-9]+\.[0-9]+-android-arm64\.apk$
```

This selects only the canonical signed arm64 release APK, excluding Windows, macOS, debug validation APKs, and other artifacts. Obtainium's GitHub source reads Releases and supports filtering APK assets; runtime update installation has not been verified on every Android device. Keep the package ID and signing key unchanged so Android can install updates over the existing app.

## AUR validation and submission

`packaging/aur/PKGBUILD` tracks `integration/yw`, initializes submodules, and builds with CMake/Ninja. The AUR validation job checks `.SRCINFO`, runs namcap, and builds the package as an unprivileged user in an Arch container. It inspects the package file list to ensure the fork's command, desktop file, icon, and separate XDG data directories do not overwrite official Azahar. The workflow does not publish to AUR; AUR submission is a manual maintainer action.

Once `azahar-yw-git` has been submitted to AUR, CachyOS/Arch users can install it with `yay -S azahar-yw-git`, or clone its AUR repository and run `makepkg -si`. The package uses `azahar-yw`, `azahar-yw.desktop`, and `azahar-yw` icon names; it does not claim to provide or conflict with official `azahar`.

## After a release

- Confirm that the GitHub Release has the Windows installer and ZIP, macOS Universal ZIP, signed Android APK, and `SHA256SUMS.txt`.
- Verify the published APK signature and checksum; test Obtainium discovery and update on Android before claiming that workflow.
- Check that the APK package ID is `io.github.alexandergg0520.azaharyw` and that its version code is greater than the prior release.
- Test desktop platforms and game-level compatibility separately from build success.
- Keep Android Level256/Pretendo/HOME compatibility labeled unverified unless tested on Android hardware.
- Never bundle Nintendo firmware, HOME system content, games, or copyrighted ROMs.
