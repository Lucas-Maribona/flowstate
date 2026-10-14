#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 || ( $# -eq 1 && $1 != --qml ) ]]; then
    printf 'Usage: %s [--qml]\n' "$0" >&2
    exit 2
fi
if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ]]; then
    printf 'Run this demo inside a Wayland desktop session (WAYLAND_DISPLAY and XDG_RUNTIME_DIR are required).\n' >&2
    exit 1
fi
if ! command -v foot >/dev/null 2>&1; then
    printf 'Install foot to run the interactive nested demo.\n' >&2
    exit 1
fi
if ! command -v setsid >/dev/null 2>&1; then
    printf 'Install util-linux (setsid) to run the interactive nested demo.\n' >&2
    exit 1
fi

socket="flowstate-$(id -u)-$$"
setsid ./build/flowstate-nested "$socket" --gap "${FLOWSTATE_GAP:-12}" &
server_pid=$!
client_pids=()
cleanup() {
    for pid in "${client_pids[@]}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    for pid in "${client_pids[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
    kill -TERM "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
}
trap cleanup EXIT

for ((attempt = 0; attempt < 100; ++attempt)); do
    if [[ -S "${XDG_RUNTIME_DIR}/${socket}" ]]; then
        printf 'Nested Flowstate is running on WAYLAND_DISPLAY=%s\n' "$socket"
        if [[ ${1:-} == --qml ]]; then
            QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY="$socket" setsid ./build/flowstate &
            client_pids+=("$!")
            count=2
        else
            count=3
        fi
        for ((number = 1; number <= count; ++number)); do
            WAYLAND_DISPLAY="$socket" setsid foot --app-id "flowstate-demo-${number}" &
            client_pids+=("$!")
        done
        printf 'Hover or click to focus; use Ctrl+Alt+J/K to switch. Press Ctrl+C to stop.\n'
        if [[ ${1:-} == --qml ]]; then
            running=("$server_pid" "${client_pids[@]}")
            while (( ${#running[@]} )); do
                finished=
                if wait -n -p finished "${running[@]}"; then
                    status=0
                else
                    status=$?
                fi
                if [[ -z "$finished" ]]; then
                    exit "$status"
                fi
                if [[ "$finished" == "$server_pid" ]]; then
                    exit "$status"
                fi
                if (( status != 0 )); then
                    printf 'A nested client exited with status %d.\n' "$status" >&2
                    exit "$status"
                fi
                remaining=()
                for pid in "${running[@]}"; do
                    if [[ "$pid" != "$finished" ]]; then
                        remaining+=("$pid")
                    fi
                done
                running=("${remaining[@]}")
            done
        else
            status=0
            for pid in "${client_pids[@]}"; do
                wait "$pid" || status=$?
            done
            exit "$status"
        fi
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        wait "$server_pid"
        exit 1
    fi
    sleep 0.05
done

printf 'Flowstate did not create its nested Wayland socket.\n' >&2
exit 1
