#!/usr/bin/env bash
#
# Convert the vendored QuickJS / QuickJS-NG engine sources into pinned git
# submodules, preserving our hand-written local changes as patch files.
#
# What it does (default / full run):
#   1. Resolves the target upstream commit for each engine.
#   2. Generates a patch capturing the local edits in the current vendored tree
#      relative to that upstream commit -> tools/patches/<engine>/.
#   3. Replaces the vendored directory with a submodule pinned to that commit.
#   4. Re-applies the patch so the working tree stays buildable (the submodule is
#      left "dirty" with the local changes, which is expected for this pattern).
#
# Engines:
#   quickjs     -> https://github.com/bellard/quickjs      @ latest (default HEAD)
#   quickjs_ng  -> https://github.com/quickjs-ng/quickjs   @ 3c8f3d68953955950074c41c6e4d999562ae82a7
#
# Modes:
#   (no args)         full conversion described above
#   --patches-only    only (re)generate the patch files; do NOT touch submodules
#                     (safe, non-destructive preview)
#   --apply-only      re-apply existing patches onto already-converted submodules
#                     (use on a fresh checkout / in CI after `submodule update`)
#
# The script never commits; it prints the commands to review and commit at the end.

set -euo pipefail

# ---- engine table (bash 3.2-friendly parallel arrays) ----------------------
ENGINE_NAMES=(quickjs quickjs_ng)
ENGINE_SUBDIRS=(source source_ng)
ENGINE_REPOS=(https://github.com/bellard/quickjs https://github.com/quickjs-ng/quickjs)
# Use HEAD to mean "latest default-branch commit"; otherwise pin to the given sha.
# quickjs   : bellard master @ 04be246 (vendored base ~= this) -> auto-diff a clean patch.
# quickjs_ng: pinned to master @ d950d55 (BC_VERSION 27). The vendored source was
#             v0.15.1 (BC_VERSION 26), so a diff can't isolate our edits; the patch
#             is hand-migrated (see tools/patches/quickjs_ng) and used as-is.
ENGINE_REFS=(04be246001599f5995fa2f2d8c91a0f198d3f34c d950d55e950dd7994a96c669c31efa967b8b79f3)

# Per-engine patch handling:
#   auto   -> regenerate the patch by diffing the vendored files vs upstream
#   manual -> use the committed patch as-is (do NOT regenerate)
ENGINE_PATCH_MODE=(auto manual)

# Only these files carry hand-written NativeScript changes, so the (auto) patch is
# scoped to them. Everything else comes verbatim from the pinned upstream commit.
PATCH_FILES=(quickjs.c quickjs.h)

PATCH_NAME="0001-nativescript-local-changes.patch"

# ---- setup -----------------------------------------------------------------
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

NAPI_QJS_REL="test-app/runtime/src/main/cpp/napi/quickjs"
PATCHES_REL="tools/patches"

command -v rsync >/dev/null || { echo "error: rsync is required" >&2; exit 1; }

log()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m warning:\033[0m %s\n' "$*" >&2; }

resolve_commit() {
  local repo="$1" ref="$2"
  if [ "$ref" = "HEAD" ]; then
    git ls-remote "$repo" HEAD | awk '{print $1}'
  else
    echo "$ref"
  fi
}

# Generate a patch of the local edits in $vendored relative to $repo@$commit,
# scoped to the files in PATCH_FILES (the ones we hand-edit). Overlaying only
# those files onto a pristine upstream checkout means the diff is purely our
# changes, with none of the upstream drift in the surrounding files.
gen_patch() {
  local vendored="$1" repo="$2" commit="$3" patch_out="$4"
  local tmp f
  tmp="$(mktemp -d)"
  git -C "$tmp" init -q
  git -C "$tmp" config core.autocrlf false
  git -C "$tmp" remote add origin "$repo"
  git -C "$tmp" fetch -q --depth 1 origin "$commit"
  git -C "$tmp" checkout -q FETCH_HEAD
  # Overlay only the hand-edited files onto the pristine upstream checkout.
  for f in "${PATCH_FILES[@]}"; do
    if [ -f "$vendored/$f" ]; then
      cp "$vendored/$f" "$tmp/$f"
    else
      warn "$(basename "$vendored"): patch file '$f' missing in vendored tree"
    fi
  done
  git -C "$tmp" add -- "${PATCH_FILES[@]}"
  mkdir -p "$(dirname "$patch_out")"
  git -C "$tmp" diff --cached --binary -- "${PATCH_FILES[@]}" >"$patch_out"
  rm -rf "$tmp"
}

apply_patch() {
  local subdir="$1" patch_abs="$2"
  if [ ! -s "$patch_abs" ]; then
    log "  no local changes to apply ($patch_abs is empty)"
    return 0
  fi
  git -C "$subdir" apply --whitespace=nowarn "$patch_abs"
}

to_submodule() {
  local subdir_rel="$1" repo="$2" commit="$3"
  # Drop the vendored copy from index + working tree, then clear any leftovers.
  git rm -r -q --ignore-unmatch "$subdir_rel" || true
  rm -rf "$subdir_rel"
  # Add the submodule (non-recursive: upstream's own test submodules stay off).
  git submodule add --force "$repo" "$subdir_rel"
  git -C "$subdir_rel" checkout -q "$commit"
  git add .gitmodules "$subdir_rel"
}

MODE="full"
case "${1:-}" in
  --patches-only) MODE="patches" ;;
  --apply-only)   MODE="apply" ;;
  "")             MODE="full" ;;
  *) echo "usage: $0 [--patches-only|--apply-only]" >&2; exit 2 ;;
esac

# ---- guard: don't clobber an existing submodule on a full run --------------
if [ "$MODE" = "full" ] && [ -f .gitmodules ]; then
  for i in "${!ENGINE_NAMES[@]}"; do
    if git config -f .gitmodules --get-regexp path 2>/dev/null \
        | awk '{print $2}' | grep -qx "$NAPI_QJS_REL/${ENGINE_SUBDIRS[$i]}"; then
      echo "error: '$NAPI_QJS_REL/${ENGINE_SUBDIRS[$i]}' is already a submodule." >&2
      echo "       Use --apply-only to re-apply patches, or remove it first." >&2
      exit 1
    fi
  done
fi

# ---- run -------------------------------------------------------------------
declare -a PINNED
for i in "${!ENGINE_NAMES[@]}"; do
  name="${ENGINE_NAMES[$i]}"
  subdir_rel="$NAPI_QJS_REL/${ENGINE_SUBDIRS[$i]}"
  repo="${ENGINE_REPOS[$i]}"
  patch_abs="$REPO_ROOT/$PATCHES_REL/$name/$PATCH_NAME"

  if [ "$MODE" = "apply" ]; then
    log "$name: re-applying $PATCHES_REL/$name/$PATCH_NAME"
    apply_patch "$subdir_rel" "$patch_abs"
    continue
  fi

  log "$name: resolving ${ENGINE_REFS[$i]} on $repo"
  commit="$(resolve_commit "$repo" "${ENGINE_REFS[$i]}")"
  PINNED+=("$name -> $commit")
  log "$name: target commit $commit"

  if [ "${ENGINE_PATCH_MODE[$i]}" = "manual" ]; then
    if [ ! -s "$patch_abs" ]; then
      warn "$name: manual patch '$PATCHES_REL/$name/$PATCH_NAME' is missing/empty."
    else
      log "$name: using hand-migrated patch $PATCHES_REL/$name/$PATCH_NAME (not regenerated)"
    fi
  elif [ -d "$subdir_rel" ]; then
    log "$name: generating patch -> $PATCHES_REL/$name/$PATCH_NAME"
    gen_patch "$subdir_rel" "$repo" "$commit" "$patch_abs"
    lines="$(wc -l <"$patch_abs" | tr -d ' ')"
    log "$name: patch is $lines line(s)"
    [ "$lines" -eq 0 ] && warn "$name: patch is empty (no local changes vs $commit)"
  elif [ "$MODE" = "patches" ]; then
    warn "$name: vendored dir '$subdir_rel' missing; cannot diff. Skipping patch."
    continue
  fi

  if [ "$MODE" = "full" ]; then
    log "$name: converting '$subdir_rel' to submodule @ $commit"
    to_submodule "$subdir_rel" "$repo" "$commit"
    log "$name: applying local patch onto the submodule"
    apply_patch "$subdir_rel" "$patch_abs"
  fi
done

# ---- summary ---------------------------------------------------------------
echo
log "Done (mode: $MODE)."
if [ "${#PINNED[@]:-0}" -gt 0 ]; then
  echo "Pinned commits:"
  printf '  %s\n' "${PINNED[@]}"
fi
if [ "$MODE" = "full" ]; then
  cat <<EOF

Review, then commit:
  git add .gitmodules $NAPI_QJS_REL/source $NAPI_QJS_REL/source_ng $PATCHES_REL
  git commit -m "chore: vendor quickjs/quickjs_ng as pinned submodules + local patches"

Notes:
  - The submodules are left dirty (local patches applied) so the build keeps
    working. Those working-tree edits are NOT part of the parent commit.
  - On a fresh checkout / in CI, run:
      git submodule update --init $NAPI_QJS_REL/source $NAPI_QJS_REL/source_ng
      scripts/vendor-engines-as-submodules.sh --apply-only
EOF
fi
