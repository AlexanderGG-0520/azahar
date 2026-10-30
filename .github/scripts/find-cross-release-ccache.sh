#!/usr/bin/env bash
# Find a compiler cache from a successful release workflow on a sibling branch.
# Called only when the current branch's actions/cache misses.
set -euo pipefail
if [[ $# -ne 1 || ! "$1" =~ ^yw-ccache-v1-(windows-msys2|macos-(arm64|x86_64)|android-arm64|arch-x86_64|macos-universal)$ ]]; then
  echo "Usage: $0 <validated yw-ccache-v1-* artifact name>" >&2
  exit 2
fi
: "${GITHUB_OUTPUT:?GitHub Actions output is required}"
: "${GITHUB_REPOSITORY:?GitHub repository is required}"
artifact_name="$1"
echo 'run_id=' >> "$GITHUB_OUTPUT"

# Optional warm start. API errors or absent artifacts must not break a release.
if ! command -v gh >/dev/null 2>&1 || [[ -z "${GH_TOKEN:-}" ]]; then
  echo 'Cross-release ccache: GitHub CLI/token unavailable; cold build.'
  exit 0
fi

candidates="$(gh api --method GET \
  "repos/$GITHUB_REPOSITORY/actions/artifacts" \
  -f "name=$artifact_name" -f per_page=100 \
  --jq '
    [.artifacts[]
      | select(.expired == false)
      | select(.workflow_run != null)
      | select(.workflow_run.repository_id == .workflow_run.head_repository_id)
      | select(.workflow_run.head_branch | test("^release/yw-v[0-9]+[.][0-9]+[.][0-9]+$"))]
    | sort_by(.created_at) | reverse | .[:12][]
    | [.workflow_run.id, .id] | @tsv
  ' 2>/dev/null)" || {
    echo 'Cross-release ccache: lookup failed; cold build.'
    exit 0
  }

while IFS=$'\t' read -r source_run artifact_id; do
  [[ "$source_run" =~ ^[0-9]+$ && "$artifact_id" =~ ^[0-9]+$ ]] || continue
  [[ "$source_run" != "${GITHUB_RUN_ID:-}" ]] || continue
  # Require a successful push from this repo's release workflow, not PR artifacts.
  metadata="$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$source_run" \
    --jq '[.status, .conclusion, .event, .head_branch, .path] | @tsv' 2>/dev/null)" || continue
  IFS=$'\t' read -r status conclusion event source_branch workflow_path <<< "$metadata"
  if [[ "$status" == completed && "$conclusion" == success &&
        "$event" == push &&
        "$source_branch" =~ ^release/yw-v[0-9]+\.[0-9]+\.[0-9]+$ &&
        "$workflow_path" == .github/workflows/yw-release.yml ]]; then
    echo "run_id=$source_run" >> "$GITHUB_OUTPUT"
    echo "Cross-release ccache: run $source_run ($source_branch), artifact $artifact_id."
    exit 0
  fi
done <<< "$candidates"

echo 'Cross-release ccache: no successful release seed; cold build.'
