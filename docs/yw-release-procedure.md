# YW fork prerelease procedure

The existing `.github/workflows/yw-release.yml` publishes Windows MSYS2 installer/portable ZIP, macOS universal ZIP, and a privately signed Vanilla Android APK. It also validates the `azahar-yw-git` Arch VCS package without submitting it to AUR. Linux desktop users can build from source or use the AUR package once it is published.

## Android signing setup

Create and retain one long-lived Android release keystore outside the repository. Store its base64-encoded keystore and credentials as repository Actions secrets:

- `ANDROID_KEYSTORE_B64`
- `ANDROID_KEY_ALIAS`
- `ANDROID_KEYSTORE_PASS`

The workflow decodes the keystore into a temporary runner file and removes it on exit. Every run on a `release/yw-vX.Y.Z` branch, including a dry-run, requires these secrets and fails in `prepare` before any platform build if one is missing. Release-branch dry-runs therefore generate the same fixed-key-signed, canonically named APK intended for publication; only the explicit publish gate controls whether a GitHub Release is created. Debug-signed `-debug.apk` files are restricted to development Android validation on `integration/yw` and are never considered release candidates. Do not regenerate or replace this key after distribution begins: Android will reject updates signed by another certificate.

Vanilla release APKs use package ID `io.github.alexandergg0520.azaharyw`. Their release `versionName` matches the `yw-vX.Y.Z` tag plus the `-vanilla` flavor suffix. `versionCode` is encoded from release semver (`major * 1,000,000,000 + minor * 1,000,000 + patch * 1,000`) for monotonic release ordering; minor and patch must each be below 1000. Development APKs without a release tag use the existing time-based version code and a `yw-dev-<sha>` version name.

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

For manual execution, choose the release branch, select `build_target: all`, and leave `dry_run: true` first. This builds and verifies publication-ready assets, including the fixed-key-signed Android APK, without publishing. The signing secrets must already be configured or `prepare` fails before the desktop builds start. An actual manual publish requires `build_target: all` and an explicit `dry_run: false`; it is accepted only on a `release/yw-vX.Y.Z` branch and requires the same signing secrets. Alternatively, the existing Release Trigger Marker on a release branch triggers the all-target publish path. Marker-only commits tag their parent source commit.

`integration/yw` accepts only Android validation: an integration push touching build inputs runs an Android-only, non-publishing build; a manual integration run must select `build_target: android` and `dry_run: true`. The workflow rejects other branches and any Android-only publish attempt. No development branch push can create or update a GitHub Release.

GitHub permits `workflow_dispatch` only when that workflow file exists on the repository's default branch. The repository default is currently `master`, where `yw-release.yml` is absent. Therefore the Run workflow UI/API will not be available until this workflow definition is merged or otherwise added to `master`. After that, select `integration/yw` in the branch selector for an Android dry-run. Before then, merging this change into `integration/yw` enables the path-limited integration push validation without adding another workflow.

## Jobs and publication gate

- `build_target: all` runs Windows, macOS, Android, and AUR validation.
- `build_target: android` runs only the Android APK job and uploads the APK as a GitHub Actions Artifact. It skips desktop and AUR jobs and can never publish.
- `build_target: pacman` builds and validates the Arch package only. With `dry_run: true` it uploads a validation package artifact without deploying. With `dry_run: false` on a release branch, it publishes the signed repository to GitHub Pages without creating or changing a GitHub Release.
- macOS arm64 and x86_64 builds run in parallel. Windows, macOS, Android, and AUR compiler caches are restored across release-branch runs, so a cold first build remains slower while later builds can reuse unchanged C++ compilation results.
- `verify-assets` waits for Windows, macOS, Android, and AUR validation. On release branches it also requires the canonical fixed-key-signed arm64 APK name, so a debug APK cannot make a release dry-run pass. It verifies the installer, portable ZIP, Universal ZIP, APK structure/package/version/signature, and release notes; then it writes a `SHA256SUMS.txt` covering all four distributable files.
- `publish` requires the explicit release gate and the successful verified payload. Any missing artifact or failed validation prevents publication.

The canonical release APK is `azahar-yw-vX.Y.Z-android-arm64.apk`. The release is a GitHub prerelease; the same APK is attached directly as a release asset so Obtainium can discover it. Android-only development dry-runs on `integration/yw` appear only as Actions Artifacts and may use a debug signature; they are not release candidates.

## Obtainium

Install Obtainium from its [official GitHub Releases](https://github.com/ImranR98/Obtainium/releases). Add `https://github.com/AlexanderGG-0520/azahar` as a GitHub source, enable **Include prereleases**, and set **Filter APKs by Regular Expression** to:

```regex
^azahar-yw-v[0-9]+\.[0-9]+\.[0-9]+-android-arm64\.apk$
```

This selects only the canonical signed arm64 release APK, excluding Windows, macOS, debug validation APKs, and other artifacts. Obtainium's GitHub source reads Releases and supports filtering APK assets; runtime update installation has not been verified on every Android device. Keep the package ID and signing key unchanged so Android can install updates over the existing app.

## Arch package validation

`packaging/aur/PKGBUILD` tracks `integration/yw`, initializes submodules, and builds with CMake/Ninja. The Arch validation job checks `.SRCINFO`, runs namcap, and builds the package as an unprivileged user in an Arch container. It inspects the package file list to ensure the fork's command, desktop file, icon, and separate XDG data directories do not overwrite official Azahar. The workflow does not submit to AUR.

## Self-hosted pacman repository

The `azahar-yw-git` package can be distributed through a signed pacman repository hosted on GitHub Pages. The initial repository bootstrap is associated with the existing `yw-v0.4.0` release line and does not create a new application version. Its stable URL is `https://alexandergg-0520.github.io/azahar/pacman/$arch`.

Publishing requires `PACMAN_REPO_GPG_PRIVATE_KEY` and `PACMAN_REPO_GPG_PASSPHRASE` Actions secrets plus Pages configured with **GitHub Actions** as the deployment source. The workflow signs both the package and repository database; clients must import and locally trust the public key before enabling the repository. See [pacman repository setup](pacman-repository.md) for owner setup, key verification, pacman.conf, install, and update instructions.

## After a release

- Confirm that the GitHub Release has the Windows installer and ZIP, macOS Universal ZIP, signed Android APK, and `SHA256SUMS.txt`.
- Verify the published APK signature and checksum; test Obtainium discovery and update on Android before claiming that workflow.
- Check that the APK package ID is `io.github.alexandergg0520.azaharyw` and that its version code is greater than the prior release.
- Test desktop platforms and game-level compatibility separately from build success.
- Keep Android Level256/Pretendo/HOME compatibility labeled unverified unless tested on Android hardware.
- Never bundle Nintendo firmware, HOME system content, games, or copyrighted ROMs.
