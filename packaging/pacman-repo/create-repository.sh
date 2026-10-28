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

for database_kind in db files; do
  database="$repo_dir/azahar-yw.$database_kind"
  # repo-add creates symlinks for both conventional database URLs. Pages
  # artifacts cannot contain links, so materialize each URL as a regular copy.
  if [[ -L "$database" ]]; then
    database_copy="$repo_dir/azahar-yw.$database_kind.tmp"
    cp -L "$database" "$database_copy"
    mv "$database_copy" "$database"
  fi
  [[ -f "$database" && ! -L "$database" ]] || {
    echo "Pacman $database_kind database URL is not a regular file." >&2
    exit 1
  }

  gpg --batch --yes --pinentry-mode loopback --passphrase-file "$passphrase_file" \
    --local-user "$fingerprint" --detach-sign --output "$database.sig" "$database"
done
gpg --batch --yes --armor --export "$fingerprint" > "$site_root/pacman-repo-key.asc"

gpg --batch --verify "$package.sig" "$package"
for database_kind in db files; do
  database="$repo_dir/azahar-yw.$database_kind"
  gpg --batch --verify "$database.sig" "$database"
done
remaining_links="$(find "$site_root" -type l -print)"
if [[ -n "$remaining_links" ]]; then
  printf 'Pacman repository contains unsupported symbolic links:\n%s\n' "$remaining_links" >&2
  exit 1
fi

echo "Pacman repository signing fingerprint: $fingerprint"
