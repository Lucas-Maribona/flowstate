#!/usr/bin/env bash
set -euo pipefail

if ! command -v foot >/dev/null 2>&1; then
    printf 'Install foot to run the interactive nested demo.\n' >&2
    exit 1
fi

socket="flowstate-$(id -u)-$$"
./build/flowstate-nested "$socket" --gap "${FLOWSTATE_GAP:-12}" &
server_pid=$!
client_pids=()
cleanup() {
    for pid in "${client_pids[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    kill -TERM "$server_pid" 2>/dev/null || true
    for pid in "${client_pids[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT

for ((attempt = 0; attempt < 100; ++attempt)); do
    if [[ -S "${XDG_RUNTIME_DIR}/${socket}" ]]; then
        printf 'Nested Flowstate is running on WAYLAND_DISPLAY=%s\n' "$socket"
        for number in 1 2 3; do
            WAYLAND_DISPLAY="$socket" foot --app-id "flowstate-demo-${number}" &
            client_pids+=("$!")
        done
        printf 'Three terminals opened. Click a tile to focus; use Ctrl+Alt+J/K to switch.\n'
        for pid in "${client_pids[@]}"; do
            wait "$pid" || true
        done
        exit 0
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        wait "$server_pid"
        exit 1
    fi
    sleep 0.05
done

printf 'Flowstate did not create its nested Wayland socket.\n' >&2
exit 1
