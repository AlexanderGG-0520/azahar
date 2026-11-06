# Signed pacman repository (Azahar YW)

The public repository `AlexanderGG-0520/azahar` builds the `azahar-yw-git` binary package from `integration/yw` and publishes an Arch Linux **x86_64** repository through GitHub Pages.

- Repository database: `https://alexandergg-0520.github.io/azahar/pacman/x86_64/azahar-yw.db`
- Signing public key: `https://alexandergg-0520.github.io/azahar/pacman-repo-key.asc`
- Workflow: `.github/workflows/yw-pacman-repo.yml`
- Package/repository names: `azahar-yw-git` / `azahar-yw`

## One-time maintainer setup

1. In **Settings → Pages**, select **GitHub Actions** as the publication source. The built-in `GITHUB_TOKEN` cannot enable Pages on a previously unconfigured repository.
2. In **Settings → Secrets and variables → Actions**, configure the **existing** repository signing key as one of `PACMAN_GPG_PRIVATE_KEY`, `PACMAN_GPG_KEY`, or `PACMAN_REPO_GPG_PRIVATE_KEY`. The secret can be an ASCII-armored private key or its base64-encoded contents.
3. If the private key requires a passphrase, set `PACMAN_GPG_PASSPHRASE`, `PACMAN_GPG_PASSWORD`, or `PACMAN_REPO_GPG_PASSPHRASE`.
4. **Reuse the old GPG signing key** whenever possible. Replacing the key changes the fingerprint and requires users to verify and trust the new key. Do not publish the private key in git or in release assets.

The workflow fails closed if no signing key is configured. The private key is used only in the separate signing job, after the package build, and is not included in the Pages output.

## Publishing

- A change to the package files, workflow, or `.github/pacman-repo-trigger/**` on `integration/yw` starts a build.
- Publishing a new GitHub Release starts a build (workflow is also present on the default branch `master`).
- **Actions → Azahar YW pacman repository → Run workflow** runs it manually from a branch where the workflow is available.

The workflow builds as an unprivileged user, signs the package, creates the `azahar-yw.db` / `azahar-yw.files` databases and their detached signatures, and publishes the package plus public key. The Pages deployment is only attempted after all prior steps succeed.

**Verification before announcing success:** the workflow must finish with a successful **deploy** job; the database and `.sig` files must return HTTP 200; an Arch/CachyOS client must successfully run `pacman -Sy` against the repository without weakening signature verification. The existence of a workflow or build artifact alone is not proof that the repository is live.

## Client setup

Download the public key, verify its fingerprint with a trusted source, then add and locally sign it with `pacman-key`. Do not trust an unknown key solely because it is downloadable.

Add this repository to `/etc/pacman.conf`:

```ini
[azahar-yw]
SigLevel = Required DatabaseRequired TrustedOnly
Server = https://alexandergg-0520.github.io/azahar/pacman/$arch
```

Then run `sudo pacman -Syu` followed by `sudo pacman -S azahar-yw-git`. **Never** change `SigLevel` to `Never` as a workaround.

Only **x86_64** binaries are published by this workflow. The existing PKGBUILD remains available at `packaging/aur/` for source builds.
