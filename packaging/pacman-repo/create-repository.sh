#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <package-file> <site-root>" >&2
  exit 2
fi

package_file="$1"
site_root="$2"
repo_dir="$site_root/pacman/x86_64"

[[ -s "$package_file" ]] || { echo "Package file is missing or empty." >&2; exit 1; }
[[ -n "${PACMAN_REPO_GPG_PASSPHRASE:-}" ]] || {
  echo "PACMAN_REPO_GPG_PASSPHRASE must be set." >&2
  exit 1
}

fingerprint="$(gpg --batch --with-colons --list-secret-keys |
  awk -F: '$1 == "fpr" { print $10; exit }')"
[[ "$fingerprint" =~ ^[A-F0-9]{40,64}$ ]] || {
  echo "Could not determine the pacman repository signing key fingerprint." >&2
  exit 1
}

install -d "$repo_dir"
install -m 644 "$package_file" "$repo_dir/"
package="$repo_dir/$(basename "$package_file")"
passphrase_file="$(mktemp)"
trap 'rm -f "$passphrase_file"' EXIT
chmod 600 "$passphrase_file"
printf '%s' "$PACMAN_REPO_GPG_PASSPHRASE" > "$passphrase_file"

gpg --batch --yes --pinentry-mode loopback --passphrase-file "$passphrase_file" \
  --local-user "$fingerprint" --detach-sign --output "$package.sig" "$package"
repo-add --include-sigs "$repo_dir/azahar-yw.db.tar.zst" "$package"

# repo-add creates two conventional URL symlinks, .db and .files. GitHub
# Pages artifacts reject ALL symlinks, even if the .db link was materialized.
# Keep both URL paths as regular files, retaining their compressed originals.
for name in azahar-yw.db azahar-yw.files; do
  link="$repo_dir/$name"
  if [[ -L "$link" ]]; then
    regular_copy="$(mktemp "$repo_dir/.pacman-materialize.XXXXXXXX")"
    cp -L -- "$link" "$regular_copy"
    chmod 0644 "$regular_copy"
    mv -f -- "$regular_copy" "$link"
  fi
  [[ -s "$link" && -f "$link" && ! -L "$link" ]] || {
    echo "Pacman repository URL $name is not a nonempty regular file." >&2
    exit 1
  }
done
database="$repo_dir/azahar-yw.db"

gpg --batch --yes --pinentry-mode loopback --passphrase-file "$passphrase_file" \
  --local-user "$fingerprint" --detach-sign --output "$database.sig" "$database"
gpg --batch --yes --armor --export "$fingerprint" > "$site_root/pacman-repo-key.asc"

gpg --batch --verify "$package.sig" "$package"
gpg --batch --verify "$database.sig" "$database"
test -z "$(find "$site_root" -type l -print -quit)"

echo "Pacman repository signing fingerprint: $fingerprint"
