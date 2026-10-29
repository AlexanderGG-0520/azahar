#!/usr/bin/env bash
# Regression test: repo-add produces .db AND .files symlinks, which Pages rejects.
# Use a real temporary GPG key and a stub repo-add to verify both are materialized.
set -euo pipefail

source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
temp_dir="$(mktemp -d)"
trap 'rm -rf "$temp_dir"' EXIT
export GNUPGHOME="$temp_dir/gnupg"
install -d -m 700 "$GNUPGHOME"
gpg --batch --pinentry-mode loopback --passphrase ci-temporary-only \
  --quick-generate-key "Pacman Repo Symlink Test" rsa2048 sign 1d >/dev/null
mkdir -p "$temp_dir/bin"
cat >"$temp_dir/bin/repo-add" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
[[ "$#" == 3 && "$1" == "--include-sigs" ]]
archive="$2"
[[ "$archive" == *.db.tar.zst ]]
dir="$(dirname "$archive")"
base="$(basename "$archive" .db.tar.zst)"
printf 'temporary db content\n' > "$archive"
printf 'temporary files content\n' > "$dir/$base.files.tar.zst"
ln -s "$(basename "$archive")" "$dir/$base.db"
ln -s "$base.files.tar.zst" "$dir/$base.files"
MOCK
chmod +x "$temp_dir/bin/repo-add"
package="$temp_dir/azahar-yw-git-test.pkg.tar.zst"
printf 'test package\n' > "$package"
site="$temp_dir/site"
PATH="$temp_dir/bin:$PATH" PACMAN_REPO_GPG_PASSPHRASE=ci-temporary-only \
  bash "$source_root/create-repository.sh" "$package" "$site"

for name in azahar-yw.db azahar-yw.files; do
  path="$site/pacman/x86_64/$name"
  [[ -s "$path" && -f "$path" && ! -L "$path" ]]
done
[[ -z "$(find "$site" -type l -print -quit)" ]]
[[ -s "$site/pacman-repo-key.asc" ]]
gpg --batch --verify "$site/pacman/x86_64/azahar-yw.db.sig" \
  "$site/pacman/x86_64/azahar-yw.db"
echo "Signed repository symlink materialization test passed."
