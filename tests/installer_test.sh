#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { printf 'installer test failed: %s\n' "$*" >&2; exit 1; }
assert_file() { [[ -f "$1" ]] || fail "missing file: $1"; }
assert_absent() { [[ ! -e "$1" ]] || fail "unexpected path: $1"; }
assert_count() {
  local expected="$1" needle="$2" file="$3" actual
  actual="$(grep -Fc "$needle" "$file" || true)"
  [[ "$actual" == "$expected" ]] || fail "expected $expected occurrences of '$needle' in $file, got $actual"
}

run_installer() {
  local home="$1" config="$2" action="$3"
  HOME="$home" \
  CLOTHCURSOR_NO_RUNTIME=1 \
  CLOTHCURSOR_BUILD_DIR="$ROOT/build-installer-test" \
  CLOTHCURSOR_HYPR_CONFIG_HOME="$config" \
  "$ROOT/install.sh" "$action"
}

# Modern end4 Lua path.
LUA_HOME="$TMP/lua-home"
LUA_CONFIG="$LUA_HOME/.config/hypr"
mkdir -p "$LUA_CONFIG/custom"
printf '%s\n' '-- active Lua entrypoint' 'require("custom.execs")' > "$LUA_CONFIG/hyprland.lua"
printf '%b' '-- unrelated line must survive  \r\n-- Unicode: कपड़ा  \r\n' > "$LUA_CONFIG/custom/execs.lua"
cp "$LUA_CONFIG/custom/execs.lua" "$TMP/lua-execs-original"

run_installer "$LUA_HOME" "$LUA_CONFIG" install
assert_file "$LUA_HOME/.local/lib/hyprland-cloth-cursor/libclothcursor.so"
assert_file "$LUA_HOME/.local/lib/hyprland-cloth-cursor/install-manifest"
assert_file "$LUA_HOME/.local/bin/clothcursorctl"
assert_count 1 'BEGIN hyprland-cloth-cursor (managed by installer)' "$LUA_CONFIG/custom/execs.lua"
grep -Fq -- '-- unrelated line must survive' "$LUA_CONFIG/custom/execs.lua" || fail 'Lua config lost unrelated content'
first_hash="$(sha256sum "$LUA_CONFIG/custom/execs.lua" | cut -d' ' -f1)"
first_backups="$(find "$LUA_CONFIG/custom" -maxdepth 1 -name 'execs.lua.clothcursor-backup-*' | wc -l)"

run_installer "$LUA_HOME" "$LUA_CONFIG" install
second_hash="$(sha256sum "$LUA_CONFIG/custom/execs.lua" | cut -d' ' -f1)"
second_backups="$(find "$LUA_CONFIG/custom" -maxdepth 1 -name 'execs.lua.clothcursor-backup-*' | wc -l)"
[[ "$first_hash" == "$second_hash" ]] || fail 'second Lua install changed managed config'
[[ "$first_backups" == "$second_backups" ]] || fail 'second Lua install created an unnecessary backup'
assert_count 1 'BEGIN hyprland-cloth-cursor (managed by installer)' "$LUA_CONFIG/custom/execs.lua"

run_installer "$LUA_HOME" "$LUA_CONFIG" uninstall
assert_absent "$LUA_HOME/.local/lib/hyprland-cloth-cursor/libclothcursor.so"
assert_absent "$LUA_HOME/.local/lib/hyprland-cloth-cursor/install-manifest"
assert_absent "$LUA_HOME/.local/bin/clothcursorctl"
assert_count 0 'BEGIN hyprland-cloth-cursor (managed by installer)' "$LUA_CONFIG/custom/execs.lua"
cmp -s "$TMP/lua-execs-original" "$LUA_CONFIG/custom/execs.lua" || fail 'Lua uninstall did not restore unrelated bytes exactly'
grep -Fq -- '-- unrelated line must survive' "$LUA_CONFIG/custom/execs.lua" || fail 'Lua uninstall lost unrelated content'

# Legacy end4 .conf path.
CONF_HOME="$TMP/conf-home"
CONF_CONFIG="$CONF_HOME/.config/hypr"
mkdir -p "$CONF_CONFIG/custom"
printf '%s\n' '# active legacy entrypoint' 'source = ./custom/execs.conf' > "$CONF_CONFIG/hyprland.conf"
printf '%s\n' '# unrelated legacy line must survive' > "$CONF_CONFIG/custom/execs.conf"

run_installer "$CONF_HOME" "$CONF_CONFIG" install
assert_count 1 'BEGIN hyprland-cloth-cursor (managed by installer)' "$CONF_CONFIG/custom/execs.conf"
grep -Fq 'exec-once = ' "$CONF_CONFIG/custom/execs.conf" || fail 'legacy startup line missing'
run_installer "$CONF_HOME" "$CONF_CONFIG" uninstall
assert_count 0 'BEGIN hyprland-cloth-cursor (managed by installer)' "$CONF_CONFIG/custom/execs.conf"
grep -Fq -- '# unrelated legacy line must survive' "$CONF_CONFIG/custom/execs.conf" || fail 'legacy uninstall lost unrelated content'

# Malformed/duplicate markers fail before installer-owned payloads are written.
BAD_HOME="$TMP/bad-home"
BAD_CONFIG="$BAD_HOME/.config/hypr"
mkdir -p "$BAD_CONFIG/custom"
printf '%s\n' 'require("custom.execs")' > "$BAD_CONFIG/hyprland.lua"
printf '%s\n' \
  '-- BEGIN hyprland-cloth-cursor (managed by installer)' \
  '-- END hyprland-cloth-cursor (managed by installer)' \
  '-- BEGIN hyprland-cloth-cursor (managed by installer)' \
  '-- END hyprland-cloth-cursor (managed by installer)' > "$BAD_CONFIG/custom/execs.lua"
bad_hash="$(sha256sum "$BAD_CONFIG/custom/execs.lua" | cut -d' ' -f1)"
if run_installer "$BAD_HOME" "$BAD_CONFIG" install >/dev/null 2>&1; then
  fail 'duplicate managed blocks were accepted'
fi
[[ "$bad_hash" == "$(sha256sum "$BAD_CONFIG/custom/execs.lua" | cut -d' ' -f1)" ]] || fail 'malformed config changed on refusal'
assert_absent "$BAD_HOME/.local/lib/hyprland-cloth-cursor/libclothcursor.so"
assert_absent "$BAD_HOME/.local/bin/clothcursorctl"

# A Lua entrypoint that does not require custom.execs is rejected unchanged.
INACTIVE_HOME="$TMP/inactive-home"
INACTIVE_CONFIG="$INACTIVE_HOME/.config/hypr"
mkdir -p "$INACTIVE_CONFIG/custom"
printf '%s\n' '-- does not load custom.execs' > "$INACTIVE_CONFIG/hyprland.lua"
printf '%s\n' '-- unrelated inactive config' > "$INACTIVE_CONFIG/custom/execs.lua"
if run_installer "$INACTIVE_HOME" "$INACTIVE_CONFIG" install >/dev/null 2>&1; then
  fail 'inactive custom.execs target was accepted'
fi
assert_absent "$INACTIVE_HOME/.local/lib/hyprland-cloth-cursor/libclothcursor.so"
assert_absent "$INACTIVE_HOME/.local/bin/clothcursorctl"

# Never follow a pre-existing symlink at an installer-owned path.
LINK_HOME="$TMP/link-home"
LINK_CONFIG="$LINK_HOME/.config/hypr"
mkdir -p "$LINK_CONFIG/custom" "$LINK_HOME/.local/bin"
printf '%s\n' 'require("custom.execs")' > "$LINK_CONFIG/hyprland.lua"
: > "$LINK_CONFIG/custom/execs.lua"
printf '%s\n' 'do not overwrite' > "$TMP/victim"
ln -s "$TMP/victim" "$LINK_HOME/.local/bin/clothcursorctl"
if run_installer "$LINK_HOME" "$LINK_CONFIG" install >/dev/null 2>&1; then
  fail 'symlinked installer target was accepted'
fi
grep -Fq 'do not overwrite' "$TMP/victim" || fail 'symlink victim was modified'

# Refuse unrelated regular files at installer-owned paths.
COLLISION_HOME="$TMP/collision-home"
COLLISION_CONFIG="$COLLISION_HOME/.config/hypr"
mkdir -p "$COLLISION_CONFIG/custom" "$COLLISION_HOME/.local/bin"
printf '%s\n' 'require("custom.execs")' > "$COLLISION_CONFIG/hyprland.lua"
: > "$COLLISION_CONFIG/custom/execs.lua"
printf '%s\n' 'foreign controller' > "$COLLISION_HOME/.local/bin/clothcursorctl"
if run_installer "$COLLISION_HOME" "$COLLISION_CONFIG" install >/dev/null 2>&1; then
  fail 'unowned regular-file collision was accepted'
fi
grep -Fq 'foreign controller' "$COLLISION_HOME/.local/bin/clothcursorctl" || fail 'foreign regular file was overwritten'
assert_absent "$COLLISION_HOME/.local/lib/hyprland-cloth-cursor/install-manifest"

# Refuse to delete an installer-owned file that changed after installation.
TAMPER_HOME="$TMP/tamper-home"
TAMPER_CONFIG="$TAMPER_HOME/.config/hypr"
mkdir -p "$TAMPER_CONFIG/custom"
printf '%s\n' 'require("custom.execs")' > "$TAMPER_CONFIG/hyprland.lua"
: > "$TAMPER_CONFIG/custom/execs.lua"
run_installer "$TAMPER_HOME" "$TAMPER_CONFIG" install
printf '%s\n' '# local modification' >> "$TAMPER_HOME/.local/bin/clothcursorctl"
if run_installer "$TAMPER_HOME" "$TAMPER_CONFIG" uninstall >/dev/null 2>&1; then
  fail 'uninstall deleted a modified installer-owned file'
fi
assert_file "$TAMPER_HOME/.local/bin/clothcursorctl"
assert_file "$TAMPER_HOME/.local/lib/hyprland-cloth-cursor/libclothcursor.so"
assert_file "$TAMPER_HOME/.local/lib/hyprland-cloth-cursor/install-manifest"
assert_count 1 'BEGIN hyprland-cloth-cursor (managed by installer)' "$TAMPER_CONFIG/custom/execs.lua"

printf 'installer integration tests passed\n'
