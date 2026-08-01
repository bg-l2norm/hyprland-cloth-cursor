#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
LIB="${CLOTHCURSOR_LIB:-$ROOT/build/libclothcursor.so}"
CONFIG="$ROOT/tests/nested.conf"
CHILD=""
CHILD_PID=""

log() { printf '[nested-test] %s\n' "$*"; }
die() { log "ERROR: $*" >&2; exit 1; }

cleanup() {
  if [[ -n "$CHILD" ]]; then
    hyprctl -i "$CHILD" clothcursor disable >/dev/null 2>&1 || true
    hyprctl -i "$CHILD" plugin unload "$LIB" >/dev/null 2>&1 || true
  fi
  if [[ -n "$CHILD_PID" ]]; then
    kill "$CHILD_PID" >/dev/null 2>&1 || true
    wait "$CHILD_PID" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

for tool in Hyprland hyprctl jq python3; do
  command -v "$tool" >/dev/null 2>&1 || die "missing prerequisite: $tool"
done
[[ -f "$CONFIG" ]] || die "missing nested config: $CONFIG"
[[ -f "$LIB" ]] || die "missing plugin build: $LIB"

if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" || -z "${WAYLAND_DISPLAY:-}" ]]; then
  instances="$(hyprctl -j instances)" || die "could not query a parent Hyprland session"
  [[ "$(jq 'length' <<<"$instances")" == 1 ]] || die "set HYPRLAND_INSTANCE_SIGNATURE and WAYLAND_DISPLAY when multiple Hyprland sessions exist"
  export HYPRLAND_INSTANCE_SIGNATURE="$(jq -r '.[0].instance' <<<"$instances")"
  export WAYLAND_DISPLAY="$(jq -r '.[0].wl_socket' <<<"$instances")"
fi

Hyprland -c "$CONFIG" >"${TMPDIR:-/tmp}/clothcursor-nested-test.log" 2>&1 &
CHILD_PID=$!
for _ in {1..60}; do
  CHILD="$(hyprctl -j instances 2>/dev/null | jq -r --argjson pid "$CHILD_PID" '.[] | select(.pid == $pid) | .instance' || true)"
  [[ -n "$CHILD" ]] && break
  kill -0 "$CHILD_PID" 2>/dev/null || die "nested Hyprland exited; inspect ${TMPDIR:-/tmp}/clothcursor-nested-test.log"
  sleep 0.25
done
[[ -n "$CHILD" ]] || die "nested Hyprland did not become reachable"

hyprctl -i "$CHILD" output create headless >/dev/null
sleep 0.5
[[ "$(hyprctl -i "$CHILD" -j monitors all | jq '[.[] | select(.disabled == false)] | length')" == 2 ]] || die "nested compositor did not expose two active outputs"
[[ "$(hyprctl -i "$CHILD" configerrors)" == "" ]] || die "nested config has errors"

hyprctl -i "$CHILD" dispatch movecursor 100 300 >/dev/null
sleep 0.7
hyprctl -i "$CHILD" plugin load "$LIB" >/dev/null
hyprctl -i "$CHILD" clothcursor enable >/dev/null
sleep 0.7

start_ab="$(hyprctl -i "$CHILD" -j clothcursor status)"
hyprctl -i "$CHILD" dispatch movecursor 900 300 >/dev/null
sleep 0.08
mid_ab="$(hyprctl -i "$CHILD" -j clothcursor status)"
sleep 0.9
end_ab="$(hyprctl -i "$CHILD" -j clothcursor status)"

start_ba="$end_ab"
hyprctl -i "$CHILD" dispatch movecursor 100 300 >/dev/null
sleep 0.08
mid_ba="$(hyprctl -i "$CHILD" -j clothcursor status)"
sleep 0.9
end_ba="$(hyprctl -i "$CHILD" -j clothcursor status)"

python3 - "$start_ab" "$mid_ab" "$end_ab" "$start_ba" "$mid_ba" "$end_ba" <<'PY'
import json
import math
import sys

start_ab, mid_ab, end_ab, start_ba, mid_ba, end_ba = map(json.loads, sys.argv[1:])

def gate(label, start, middle, end, expected_x):
    assert start["spring_settled"] is True, (label, start)
    assert middle["spring_settled"] is False, (label, middle)
    assert end["spring_settled"] is True, (label, end)
    assert abs(end["target_x"] - expected_x) < 0.01, (label, end)
    assert math.hypot(end["target_x"] - end["body_x"], end["target_y"] - end["body_y"]) < 0.01, (label, end)
    assert end["cursor_hook_calls"] - start["cursor_hook_calls"] >= 5, (label, start, end)
    assert end["owner_output_hook_calls"] - start["owner_output_hook_calls"] >= 5, (label, start, end)
    assert end["passes_queued"] - start["passes_queued"] >= 5, (label, start, end)
    for key in ("render_rejects", "fallback_calls", "missing_image", "missing_texture", "invalid_state"):
        assert end[key] == start[key], (label, key, start, end)

gate("A->B", start_ab, mid_ab, end_ab, 900.0)
gate("B->A", start_ba, mid_ba, end_ba, 100.0)
PY

[[ "$(hyprctl -i "$CHILD" configerrors)" == "" ]] || die "nested config developed errors"
log "bidirectional two-output settlement, owner scheduling, rendering, and zero-fallback gates passed"
