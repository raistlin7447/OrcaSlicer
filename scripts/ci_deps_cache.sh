#!/usr/bin/env bash
# Finds a deps cache this run can restore, from a job on another OS than the
# deps build. `check` looks once and, for a pull request that does not change
# deps/, names a queued or running push build of the base branch with the same
# deps/ to wait for. `wait` waits for that build to save the cache.
#
# Environment: GH_TOKEN, REPO, WORKFLOW_REF, EVENT, REF, BASE_REF, KEY, and for
# `wait` also DEPS_TREE and WAIT_FOR. `check` reads deps/ from the checked-out
# commit and, for a pull request, its first parent.
# Writes hit, deps-tree and wait-for to $GITHUB_OUTPUT. A failed API call counts
# as no cache, so the run builds its own deps.
set -uo pipefail

workflow=${WORKFLOW_REF%%@*}
workflow=${workflow##*/}

out() { echo "$1" >> "${GITHUB_OUTPUT:-/dev/stdout}"; }

# A run can restore a cache saved under its own ref or under its base branch.
cache_saved() {
  local ref
  for ref in "$REF" "refs/heads/$BASE_REF"; do
    if gh api -X GET "repos/$REPO/actions/caches" -f key="$KEY" -f ref="$ref" < /dev/null \
        | jq -e --arg key "$KEY" 'any(.actions_caches[]; .key == $key)' > /dev/null; then
      return 0
    fi
  done
  return 1
}

# Prints the id of a push build of the base branch that has not finished and
# whose deps/ matches DEPS_TREE.
matching_base_build() {
  local status runs id sha
  runs=$(for status in in_progress queued pending waiting requested; do
    gh api -X GET "repos/$REPO/actions/workflows/$workflow/runs" \
      -f branch="$BASE_REF" -f event=push -f status="$status" -f per_page=10 < /dev/null \
      | jq -r '.workflow_runs[] | "\(.id) \(.head_sha)"'
  done)
  while read -r id sha; do
    [ -n "$id" ] || continue
    if [ "$(gh api "repos/$REPO/git/trees/$sha" --jq '.tree[] | select(.path == "deps") | .sha' < /dev/null)" = "$DEPS_TREE" ]; then
      echo "$id"
      return
    fi
  done <<< "$runs"
}

check() {
  DEPS_TREE=$(git rev-parse HEAD:deps)
  out "deps-tree=$DEPS_TREE"
  if cache_saved; then
    echo "Found $KEY."
    out hit=true
    return
  fi
  out hit=false
  [ "$EVENT" = pull_request ] || return 0
  if [ "$DEPS_TREE" != "$(git rev-parse HEAD^1:deps)" ]; then
    echo "This pull request changes deps/, so it builds its own deps."
    return
  fi
  local id
  id=$(matching_base_build)
  if [ -n "$id" ]; then
    echo "Build $id of $BASE_REF builds the same deps/. Waiting for its cache."
    out "wait-for=$id"
  else
    echo "No build of $BASE_REF is building these deps."
  fi
}

wait_for() {
  local id=$WAIT_FOR deadline=$((SECONDS + 2 * 60 * 60))
  while [ "$SECONDS" -lt "$deadline" ]; do
    if cache_saved; then
      echo "Found $KEY."
      out hit=true
      return
    fi
    if [ "$(gh api "repos/$REPO/actions/runs/$id" --jq .status < /dev/null)" = completed ]; then
      cache_saved && { echo "Found $KEY."; out hit=true; return; }
      # A newer push replaces a pending build, so follow one with the same deps/.
      id=$(matching_base_build)
      [ -n "$id" ] || break
      echo "Following build $id of $BASE_REF."
    fi
    sleep 60
  done
  echo "No build of $BASE_REF saved $KEY. Building deps here."
  out hit=false
}

case "${1:-}" in
  check) check ;;
  wait) wait_for ;;
  *) echo "usage: $0 check|wait" >&2; exit 2 ;;
esac
