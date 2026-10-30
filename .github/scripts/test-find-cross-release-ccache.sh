#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/bin"
cat > "$tmp/bin/gh" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
if [[ "$*" == *'/actions/artifacts'* ]]; then
  printf '101\t9001\n102\t9002\n103\t9003\n'
elif [[ "$*" == *'/actions/runs/101'* ]]; then
  printf 'completed\tfailure\tpush\trelease/yw-v0.4.2\t.github/workflows/yw-release.yml\n'
elif [[ "$*" == *'/actions/runs/102'* ]]; then
  printf 'completed\tsuccess\tpull_request\trelease/yw-v0.4.1\t.github/workflows/yw-release.yml\n'
elif [[ "$*" == *'/actions/runs/103'* ]]; then
  printf 'completed\tsuccess\tpush\trelease/yw-v0.4.1\t.github/workflows/yw-release.yml\n'
else
  exit 1
fi
MOCK
chmod +x "$tmp/bin/gh"
export PATH="$tmp/bin:$PATH" GITHUB_OUTPUT="$tmp/output" GITHUB_REPOSITORY='Example/azahar' GITHUB_RUN_ID='200' GH_TOKEN='test-token'
bash "$script_dir/find-cross-release-ccache.sh" yw-ccache-v1-arch-x86_64
[[ "$(tail -n1 "$tmp/output")" == 'run_id=103' ]]
GH_TOKEN='' bash "$script_dir/find-cross-release-ccache.sh" yw-ccache-v1-arch-x86_64
[[ "$(tail -n1 "$tmp/output")" == 'run_id=' ]]
if bash "$script_dir/find-cross-release-ccache.sh" untrusted 2>/dev/null; then
  echo 'Rejected artifact name was accepted' >&2
  exit 1
fi
echo 'Cross-release ccache discovery tests passed.'
