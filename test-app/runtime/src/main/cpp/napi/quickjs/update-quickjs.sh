#!/usr/bin/env bash
#
# update-quickjs.sh — refresh a vendored QuickJS engine from upstream master
# and re-apply NativeScript's local patches.
#
# Two variants are supported:
#   ng        quickjs-ng  -> https://github.com/quickjs-ng/quickjs.git -> ./source_ng
#   bellard   quickjs     -> https://github.com/bellard/quickjs        -> ./source
#
# We always track the upstream `master` branch.
#
# Usage:
#   ./update-quickjs.sh [ng|bellard|all]    (default: all)
#
# What it does, per selected variant:
#   1. Shallow-clones upstream master into a temp dir.
#   2. Copies every top-level *.c / *.h into the vendored source dir
#      (engine only; NativeScript's quickjs-api.c / jsr.cpp live one level up
#      and are never touched).
#   3. Re-applies the matching patch from ./patches.
#
# NativeScript only modifies the engine's quickjs.c (host-object support), so
# the patch targets that single file. If a future upstream refactor moves the
# patched lines, `patch` falls back to fuzzy matching and, failing that, writes
# a .rej for manual fixup.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_DIR="$SCRIPT_DIR/patches"

NG_REPO="https://github.com/quickjs-ng/quickjs.git"
BELLARD_REPO="https://github.com/bellard/quickjs"

err()  { printf '\033[31m%s\033[0m\n' "$*" >&2; }
info() { printf '\033[36m%s\033[0m\n' "$*"; }
ok()   { printf '\033[32m%s\033[0m\n' "$*"; }

# update_variant <name> <repo-url> <dest-dir> <patch-file>
update_variant() {
  local name="$1" repo="$2" dest="$3" patch="$4"

  info "==> [$name] updating $dest"

  if [[ ! -d "$dest" ]]; then
    err "destination '$dest' does not exist — aborting $name"
    return 1
  fi
  if [[ ! -f "$patch" ]]; then
    err "patch '$patch' not found — aborting $name"
    return 1
  fi

  # Warn if anything other than quickjs.c has uncommitted local edits: those
  # would be silently overwritten by the upstream copy.
  if git -C "$dest" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    local dirty
    dirty="$(git -C "$dest" status --porcelain -- . | awk '{print $2}' | grep -v '^quickjs\.c$' || true)"
    if [[ -n "$dirty" ]]; then
      err "WARNING: uncommitted changes in $dest (besides quickjs.c) will be overwritten:"
      err "$dirty"
      read -r -p "Continue? [y/N] " reply
      [[ "$reply" == [yY] ]] || { err "skipping $name"; return 1; }
    fi
  fi

  local tmp
  tmp="$(mktemp -d)"
  trap 'rm -rf "$tmp"' RETURN

  info "    cloning $repo (master, shallow)"
  git clone --depth 1 --branch master "$repo" "$tmp/src" >/dev/null 2>&1 \
    || git clone --depth 1 "$repo" "$tmp/src" >/dev/null 2>&1

  local rev
  rev="$(git -C "$tmp/src" rev-parse --short HEAD)"
  info "    upstream master @ $rev"

  info "    copying top-level *.c / *.h -> $dest"
  # Only top-level engine sources; never recurse into upstream's tests/, etc.
  find "$tmp/src" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \) \
    -exec cp -f {} "$dest"/ \;

  info "    applying $(basename "$patch")"
  if patch -p1 --forward --fuzz=3 -d "$dest" < "$patch" >/dev/null; then
    ok  "    [$name] done (upstream $rev + patch applied)"
  else
    err "    [$name] PATCH DID NOT APPLY CLEANLY — see *.rej in $dest"
    err "    re-generate the patch after fixing: "
    err "      git -C $dest diff --relative -- quickjs.c > $patch"
    return 1
  fi
}

main() {
  local target="${1:-all}"
  case "$target" in
    ng)
      update_variant ng "$NG_REPO" "$SCRIPT_DIR/source_ng" \
        "$PATCH_DIR/host-object-getproperty.ng.patch"
      ;;
    bellard)
      update_variant bellard "$BELLARD_REPO" "$SCRIPT_DIR/source" \
        "$PATCH_DIR/host-object-getproperty.bellard.patch"
      ;;
    all)
      update_variant ng "$NG_REPO" "$SCRIPT_DIR/source_ng" \
        "$PATCH_DIR/host-object-getproperty.ng.patch"
      update_variant bellard "$BELLARD_REPO" "$SCRIPT_DIR/source" \
        "$PATCH_DIR/host-object-getproperty.bellard.patch"
      ;;
    *)
      err "usage: $0 [ng|bellard|all]"
      exit 2
      ;;
  esac
  ok "All requested variants updated. Review 'git diff' and rebuild."
}

main "$@"
