# Azahar YW pacman repository

This repository is intended to provide `azahar-yw-git` to Arch Linux and CachyOS without relying on AUR account registration. The first repository publication bootstraps the already-published `yw-v0.4.0` release line; it does not create a new application version or move the release tag. The package recipe continues to build the `integration/yw` VCS source.

The stable repository URL is:

```text
https://alexandergg0520.github.io/azahar/pacman/$arch
```

The package database and package files are signed with a dedicated OpenPGP key. Do not disable pacman signature checking for this repository.

## Repository owner setup

Before the first publication:

1. Create a dedicated OpenPGP signing key with `gpg --full-generate-key`. Choose a signing-capable RSA key, 4096 bits, no expiry, and set a strong passphrase. Keep its private export outside the repository.
2. Export its private key directly into the GitHub Actions secret (replace the fingerprint with the one shown by GPG):

   ```sh
   gpg --armor --export-secret-keys KEY_FINGERPRINT \
     | gh secret set PACMAN_REPO_GPG_PRIVATE_KEY --repo AlexanderGG-0520/azahar
   ```

3. Add the passphrase as a second repository secret without putting it in shell history:

   ```fish
   read --silent --prompt-str "Signing key passphrase: " pacman_repo_passphrase
   printf '%s' "$pacman_repo_passphrase" \
     | gh secret set PACMAN_REPO_GPG_PASSPHRASE --repo AlexanderGG-0520/azahar
   set --erase pacman_repo_passphrase
   ```

   `gh` must be authenticated with permission to manage repository Actions secrets. The secrets are `PACMAN_REPO_GPG_PRIVATE_KEY` (ASCII-armored private key) and `PACMAN_REPO_GPG_PASSPHRASE` (key passphrase). Never commit or artifact the private key.
4. In **Settings → Pages → Build and deployment**, set **Source** to **GitHub Actions**. The Pages endpoint is not active until this has been enabled and the first deployment succeeds.
5. From the `release/yw-v0.4.0` branch, add `.github/pacman-repo-trigger/yw-v0.4.0` in a marker-only commit and push it. This runs only the Arch package build and pacman repository deployment; it does not publish another GitHub Release or rerun the Windows/macOS/Android builds.

The workflow publishes the key's public part as `pacman-repo-key.asc` and prints its fingerprint. Keep that fingerprint for client setup and publish it through a trusted channel. Never upload the private key as an artifact or commit it to Git.

To refresh the VCS package later, run the pacman-only target from a release branch with `dry_run` disabled, or update the pacman repository trigger marker. GitHub requires a `workflow_dispatch` workflow to exist on the repository's default branch; until this workflow is present there, use the marker path. The normal release marker and GitHub Release publication remain separate.

## Install on Arch Linux / CachyOS

After the first successful repository deployment, download its public key and inspect the fingerprint:

```sh
curl --fail --location \
  https://alexandergg0520.github.io/azahar/pacman-repo-key.asc \
  --output /tmp/azahar-yw-repo-key.asc
gpg --show-keys --with-fingerprint /tmp/azahar-yw-repo-key.asc
```

Compare the fingerprint with the value from the trusted `pacman-repository` Actions run. Then add and locally trust the key:

```sh
sudo pacman-key --add /tmp/azahar-yw-repo-key.asc
sudo pacman-key --lsign-key KEY_FINGERPRINT
```

Add this repository section to `/etc/pacman.conf`:

```ini
[azahar-yw]
SigLevel = Required DatabaseRequired TrustedOnly
Server = https://alexandergg0520.github.io/azahar/pacman/$arch
```

Install and update through pacman:

```sh
sudo pacman -Syu azahar-yw-git
sudo pacman -Syu
```

Remove it with:

```sh
sudo pacman -Rns azahar-yw-git
```

The package uses the `azahar-yw` executable, desktop entry, icon, and XDG data directories, so it can coexist with the official Azahar package. The hosted binary repository initially targets x86_64. AArch64 users can still build the VCS PKGBUILD locally with `makepkg -si`.

## Local package build

Before the hosted repository is active, or on an unsupported architecture, the same recipe can be built locally:

```sh
git clone --recursive https://github.com/AlexanderGG-0520/azahar.git
cd azahar/packaging/aur
makepkg -si
```

Pacman will track a locally installed package and can remove it, but updates must be rebuilt and installed manually with `makepkg -si` until the signed repository is enabled.
