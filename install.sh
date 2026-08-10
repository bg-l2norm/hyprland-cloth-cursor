#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${CLOTHCURSOR_BUILD_DIR:-$ROOT/build}"
INSTALL_DIR="${CLOTHCURSOR_INSTALL_DIR:-$HOME/.local/lib/hyprland-cloth-cursor}"
LIB_PATH="$INSTALL_DIR/libclothcursor.so"
MANIFEST_PATH="$INSTALL_DIR/install-manifest"
BIN_DIR="${CLOTHCURSOR_BIN_DIR:-$HOME/.local/bin}"
CTL_PATH="$BIN_DIR/clothcursorctl"
HYPR_CONFIG_HOME="${CLOTHCURSOR_HYPR_CONFIG_HOME:-$HOME/.config/hypr}"
LUA_EXECS="$HYPR_CONFIG_HOME/custom/execs.lua"
CONF_EXECS="$HYPR_CONFIG_HOME/custom/execs.conf"
MARK_START="-- BEGIN hyprland-cloth-cursor (managed by installer)"
MARK_END="-- END hyprland-cloth-cursor (managed by installer)"
CONF_MARK_START="# BEGIN hyprland-cloth-cursor (managed by installer)"
CONF_MARK_END="# END hyprland-cloth-cursor (managed by installer)"
TESTED_HYPRLAND_ABIS=(
  "a0136d8c04687bb36eb8a28eb9d1ff92aea99704_aq_0.12_hu_0.13_hg_0.5_hc_0.1_hlg_0.6"
  "5c9377c15f85c50648f35ca5a213754f95b93ca0_aq_0.14_hu_0.14_hg_0.5_hc_0.1_hlg_0.6"
  "efb50993780079460b0cbed1363e2166a2de1d9f_aq_0.14_hu_0.14_hg_0.5_hc_0.1_hlg_0.6"
)

log() { printf '[installer] %s\n' "$*"; }
die() { log "ERROR: $*" >&2; exit 1; }

acquire_installer_lock() {
  ((EUID != 0)) || die "do not run this installer as root or with sudo"
  command -v flock >/dev/null 2>&1 || die "missing prerequisite: flock"
  local lock_dir="${XDG_RUNTIME_DIR:-$HOME/.cache}/hyprland-cloth-cursor"
  install -d -m 0700 "$lock_dir"
  exec 9>"$lock_dir/install.lock"
  flock -x 9
}

validate_owned_paths() {
  local path
  for path in "$LIB_PATH" "$CTL_PATH" "$MANIFEST_PATH"; do
    [[ ! -L "$path" ]] || die "refusing symlinked installer target: $path"
    [[ ! -e "$path" || -f "$path" ]] || die "installer target is not a regular file: $path"
  done
}

usage() {
  cat <<'EOF'
Usage: ./install.sh [install|uninstall|status]

  install    build, test, install, persist, and enable clothcursor (default)
  uninstall  disable/unload it and remove only installer-managed files/blocks
  status     show installed/runtime/persistence status

Environment overrides used by tests/packagers:
  CLOTHCURSOR_BUILD_DIR, CLOTHCURSOR_INSTALL_DIR, CLOTHCURSOR_BIN_DIR,
  CLOTHCURSOR_HYPR_CONFIG_HOME, CLOTHCURSOR_NO_RUNTIME=1
EOF
}

require_tools() {
  local missing=()
  local tool
  for tool in cmake pkg-config python3 install Hyprland readelf ldd sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
  done
  ((${#missing[@]} == 0)) || die "missing prerequisites: ${missing[*]}"
  [[ -r /usr/include/hyprland/src/version.h ]] || die "Hyprland development headers are missing (/usr/include/hyprland/src/version.h)"
}

verify_installed_hyprland() {
  local version
  version="$(Hyprland --version 2>&1)" || die "could not query Hyprland version"
  local tested_abi
  for tested_abi in "${TESTED_HYPRLAND_ABIS[@]}"; do
    if grep -Fq "Version ABI string: $tested_abi" <<<"$version"; then
      log "verified tested Hyprland ABI: $tested_abi"
      return 0
    fi
  done

  log "WARN: this Hyprland build has not been tested with Cloth Cursor."
  log "Tested ABIs:"
  for tested_abi in "${TESTED_HYPRLAND_ABIS[@]}"; do
    log "  $tested_abi"
  done
  printf '%s\n' "$version" | sed -n '/^Hyprland /p; /^Version ABI string:/p' >&2
  log "The plugin will be compiled against your installed headers and will still refuse a runtime ABI mismatch."

  if [[ "${CLOTHCURSOR_ALLOW_UNTESTED:-0}" == 1 ]]; then
    log "continuing because CLOTHCURSOR_ALLOW_UNTESTED=1"
    return 0
  fi
  if [[ -t 0 ]]; then
    local answer
    read -r -p "[installer] Continue with this untested Hyprland build? [y/N] " answer
    [[ "$answer" == y || "$answer" == Y || "$answer" == yes || "$answer" == YES ]] && return 0
  fi
  die "installation cancelled; no files were changed (set CLOTHCURSOR_ALLOW_UNTESTED=1 for a non-interactive install)"
}

config_mode() {
  if [[ -f "$HYPR_CONFIG_HOME/hyprland.lua" ]]; then
    printf 'lua\n'
  elif [[ -f "$HYPR_CONFIG_HOME/hyprland.conf" ]]; then
    printf 'conf\n'
  else
    printf 'none\n'
  fi
}

edit_managed_block() {
  local action="$1" mode="$2" path="$3"
  [[ ! -L "$path" ]] || die "refusing symlinked config target: $path"
  [[ ! -e "$path" || -f "$path" ]] || die "config target is not a regular file: $path"
  if [[ "$action" != check ]]; then
    mkdir -p "$(dirname "$path")"
    [[ -f "$path" ]] || : > "$path"
  fi

  python3 - "$action" "$mode" "$path" "$CTL_PATH" <<'PY'
from __future__ import annotations
from datetime import datetime
from pathlib import Path
import shutil
import sys

action, mode, raw_path, ctl_path = sys.argv[1:]
path = Path(raw_path)
raw = path.read_bytes() if path.exists() else b""
try:
    text = raw.decode("utf-8")
except UnicodeDecodeError as error:
    raise SystemExit(f"{path} is not valid UTF-8: {error}") from error

if mode == "lua":
    start = "-- BEGIN hyprland-cloth-cursor (managed by installer)"
    end = "-- END hyprland-cloth-cursor (managed by installer)"
    escaped = ctl_path.replace("\\", "\\\\").replace('"', '\\"')
    block = (
        f"{start}\n"
        "-- Loads only on a fresh Hyprland session; config reloads cannot duplicate it.\n"
        'hl.on("hyprland.start", function()\n'
        f'    hl.exec_cmd("{escaped} session-start")\n'
        "end)\n"
        f"{end}"
    )
else:
    start = "# BEGIN hyprland-cloth-cursor (managed by installer)"
    end = "# END hyprland-cloth-cursor (managed by installer)"
    escaped = ctl_path.replace("\\", "\\\\").replace('"', '\\"')
    block = f'{start}\nexec-once = "{escaped}" session-start\n{end}'

lines = text.splitlines(keepends=True)
line_bodies = [line.rstrip("\r\n").strip() for line in lines]
starts = [index for index, line in enumerate(line_bodies) if line == start]
ends = [index for index, line in enumerate(line_bodies) if line == end]
if len(starts) > 1 or len(ends) > 1:
    raise SystemExit(f"duplicate managed blocks in {path}; refusing to guess")
if len(starts) != len(ends):
    raise SystemExit(f"unmatched managed block marker in {path}")
if starts and starts[0] >= ends[0]:
    raise SystemExit(f"reversed managed block markers in {path}")

eol = "\r\n" if "\r\n" in text else "\n"
rendered_block = block.replace("\n", eol)
if starts:
    start_index, end_index = starts[0], ends[0]
    prefix = "".join(lines[:start_index])
    suffix = "".join(lines[end_index + 1:])
    if action in {"install", "check"}:
        marker_eol = eol if lines[end_index].endswith(("\n", "\r")) else ""
        new_text = prefix + rendered_block + marker_eol + suffix
    else:
        new_text = prefix + suffix
elif action in {"install", "check"}:
    separator = eol if text and not text.endswith(("\n", "\r")) else ""
    new_text = text + separator + rendered_block + eol
else:
    new_text = text

if action == "check":
    raise SystemExit(0)

new_raw = new_text.encode("utf-8")
if new_raw != raw:
    if raw:
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        backup = path.with_name(path.name + f".clothcursor-backup-{stamp}")
        suffix = 1
        while backup.exists():
            backup = path.with_name(path.name + f".clothcursor-backup-{stamp}-{suffix}")
            suffix += 1
        shutil.copy2(path, backup)
    tmp = path.with_name(path.name + ".clothcursor.tmp")
    tmp.write_bytes(new_raw)
    tmp.replace(path)
PY
}

persist_preflight() {
  local mode
  mode="$(config_mode)"
  case "$mode" in
    lua)
      python3 - "$HYPR_CONFIG_HOME/hyprland.lua" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
if not re.search(r'''require\s*\(?\s*["']custom\.execs["']''', text):
    raise SystemExit("active hyprland.lua does not require custom.execs; refusing to edit an inactive file")
PY
      edit_managed_block check lua "$LUA_EXECS"
      ;;
    conf)
      grep -Eq '(^|[[:space:]])source[[:space:]]*=[[:space:]]*.*custom/execs\.conf' "$HYPR_CONFIG_HOME/hyprland.conf" ||
        die "active hyprland.conf does not source custom/execs.conf"
      edit_managed_block check conf "$CONF_EXECS"
      ;;
    none) die "no ~/.config/hypr/hyprland.lua or hyprland.conf found; refusing to guess a persistence format" ;;
  esac
}

persist_install() {
  local mode
  mode="$(config_mode)"
  case "$mode" in
    lua)
      python3 - "$HYPR_CONFIG_HOME/hyprland.lua" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
if not re.search(r'''require\s*\(?\s*["']custom\.execs["']''', text):
    raise SystemExit("active hyprland.lua does not require custom.execs; refusing to edit an inactive file")
PY
      edit_managed_block install lua "$LUA_EXECS"
      ;;
    conf)
      grep -Eq '(^|[[:space:]])source[[:space:]]*=[[:space:]]*.*custom/execs\.conf' "$HYPR_CONFIG_HOME/hyprland.conf" ||
        die "active hyprland.conf does not source custom/execs.conf"
      edit_managed_block install conf "$CONF_EXECS"
      ;;
    none) die "no ~/.config/hypr/hyprland.lua or hyprland.conf found; refusing to guess a persistence format" ;;
  esac
  if [[ "$mode" == lua ]] && command -v luac >/dev/null 2>&1; then
    luac -p "$LUA_EXECS" || die "managed Lua config did not pass syntax validation"
  fi
}

persist_remove() {
  if [[ -f "$LUA_EXECS" ]]; then
    edit_managed_block remove lua "$LUA_EXECS"
  fi
  if [[ -f "$CONF_EXECS" ]]; then
    edit_managed_block remove conf "$CONF_EXECS"
  fi
}

runtime_reachable() {
  [[ "${CLOTHCURSOR_NO_RUNTIME:-0}" != 1 ]] && command -v hyprctl >/dev/null 2>&1 && hyprctl -j plugin list >/dev/null 2>&1
}

installed_library_is_mapped() {
  local maps
  for maps in /proc/[0-9]*/maps; do
    [[ -r "$maps" ]] || continue
    if grep -Fq " $LIB_PATH" "$maps" 2>/dev/null; then
      return 0
    fi
  done
  return 1
}

build_and_test() {
  require_tools
  verify_installed_hyprland
  cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD_DIR" -j"${CLOTHCURSOR_JOBS:-2}"
  ctest --test-dir "$BUILD_DIR" --output-on-failure
  [[ -f "$BUILD_DIR/libclothcursor.so" ]] || die "build completed without libclothcursor.so"
  readelf -h "$BUILD_DIR/libclothcursor.so" >/dev/null || die "built plugin is not a valid ELF shared object"
  if ldd "$BUILD_DIR/libclothcursor.so" | grep -Fq 'not found'; then
    die "built plugin has unresolved shared-library dependencies"
  fi
}

verify_ownership() {
  local mode="$1"
  python3 - "$mode" "$MANIFEST_PATH" "$LIB_PATH" "$CTL_PATH" "$BUILD_DIR/libclothcursor.so" "$ROOT/scripts/clothcursorctl" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys

mode, manifest_raw, lib_raw, ctl_raw, desired_lib_raw, desired_ctl_raw = sys.argv[1:]
manifest, lib, ctl = map(Path, (manifest_raw, lib_raw, ctl_raw))
desired_lib, desired_ctl = map(Path, (desired_lib_raw, desired_ctl_raw))

def digest(path: Path) -> str:
    return sha256(path.read_bytes()).hexdigest()

def fail(message: str) -> None:
    raise SystemExit(f"ownership check failed: {message}")

if manifest.exists():
    values: dict[str, str] = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if not line or "=" not in line:
            fail(f"malformed manifest {manifest}")
        key, value = line.split("=", 1)
        if key in values or key not in {"schema", "lib_sha256", "ctl_sha256"}:
            fail(f"malformed manifest {manifest}")
        values[key] = value
    if values.get("schema") != "1" or set(values) != {"schema", "lib_sha256", "ctl_sha256"}:
        fail(f"unsupported or incomplete manifest {manifest}")
    for path, key in ((lib, "lib_sha256"), (ctl, "ctl_sha256")):
        if path.exists() and digest(path) != values[key]:
            fail(f"{path} differs from the installer-owned digest; refusing to overwrite or delete it")
    raise SystemExit(0)

present = [path for path in (lib, ctl) if path.exists()]
if not present:
    raise SystemExit(0)
if mode == "install" and lib.exists() and ctl.exists() and digest(lib) == digest(desired_lib) and digest(ctl) == digest(desired_ctl):
    print("[installer] adopting matching files from the pre-manifest installer")
    raise SystemExit(0)
fail("installer-owned path already exists without a valid manifest: " + ", ".join(map(str, present)))
PY
}

write_manifest() {
  local lib_hash ctl_hash temp
  lib_hash="$(sha256sum "$LIB_PATH" | cut -d' ' -f1)"
  ctl_hash="$(sha256sum "$CTL_PATH" | cut -d' ' -f1)"
  temp="$INSTALL_DIR/.install-manifest.new.$$"
  umask 077
  printf 'schema=1\nlib_sha256=%s\nctl_sha256=%s\n' "$lib_hash" "$ctl_hash" > "$temp"
  chmod 0600 "$temp"
  mv -f "$temp" "$MANIFEST_PATH"
}

install_action() {
  validate_owned_paths
  build_and_test
  verify_ownership install
  persist_preflight

  # Never overwrite a library while Hyprland has any clothcursor .so mapped.
  if runtime_reachable && hyprctl -j plugin list | python3 -c 'import json,sys; raise SystemExit(0 if any(p.get("name") == "clothcursor" for p in json.load(sys.stdin)) else 1)'; then
    CLOTHCURSOR_LIB="$LIB_PATH" "$ROOT/scripts/clothcursorctl" unload
  fi
  if installed_library_is_mapped; then
    die "installed library is still mapped by a process; refusing in-place replacement"
  fi

  install -d -m 0755 "$INSTALL_DIR" "$BIN_DIR"
  install -m 0755 "$ROOT/scripts/clothcursorctl" "$CTL_PATH"
  local temp_lib="$INSTALL_DIR/.libclothcursor.so.new.$$"
  install -m 0644 "$BUILD_DIR/libclothcursor.so" "$temp_lib"
  mv -f "$temp_lib" "$LIB_PATH"
  write_manifest

  persist_install

  if runtime_reachable; then
    if ! CLOTHCURSOR_LIB="$LIB_PATH" "$CTL_PATH" enable; then
      log "Runtime activation failed; removing autostart block so the next login stays safe."
      persist_remove
      exit 1
    fi
    log "installed and enabled in the current session"
  else
    log "installed persistently; no reachable Hyprland session was modified"
  fi

  log "Re-run command: $ROOT/install.sh install"
  log "Rollback command: $ROOT/install.sh uninstall"
}

uninstall_action() {
  validate_owned_paths
  verify_ownership uninstall
  if runtime_reachable && [[ -x "$CTL_PATH" ]]; then
    CLOTHCURSOR_LIB="$LIB_PATH" "$CTL_PATH" unload || log "WARN: runtime unload failed; files were not removed"
    if hyprctl -j plugin list | python3 -c 'import json,sys; raise SystemExit(0 if any(p.get("name") == "clothcursor" for p in json.load(sys.stdin)) else 1)'; then
      die "clothcursor is still loaded; refusing to delete a mapped library"
    fi
  fi
  if installed_library_is_mapped; then
    die "installed library is still mapped by a process; refusing deletion"
  fi

  persist_remove
  rm -f "$LIB_PATH" "$CTL_PATH" "$MANIFEST_PATH"
  rmdir "$INSTALL_DIR" 2>/dev/null || true
  log "uninstalled; unrelated Hyprland and cursor-theme settings were preserved"
}

status_action() {
  printf 'ownership manifest: %s\n' "$([[ -f "$MANIFEST_PATH" ]] && echo yes || echo no)"
  if [[ -x "$CTL_PATH" ]]; then
    CLOTHCURSOR_LIB="$LIB_PATH" "$CTL_PATH" status
  else
    printf 'installed: no\n'
  fi
  local persistent=no
  if grep -Fq 'BEGIN hyprland-cloth-cursor (managed by installer)' "$LUA_EXECS" 2>/dev/null ||
     grep -Fq 'BEGIN hyprland-cloth-cursor (managed by installer)' "$CONF_EXECS" 2>/dev/null; then
    persistent=yes
  fi
  printf 'persistent startup: %s\n' "$persistent"
}

case "${1:-install}" in
  install) acquire_installer_lock; install_action ;;
  uninstall) acquire_installer_lock; uninstall_action ;;
  status) status_action ;;
  -h|--help|help) usage ;;
  *) usage >&2; exit 2 ;;
esac
