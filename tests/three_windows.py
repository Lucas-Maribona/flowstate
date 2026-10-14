"""Exercise tiling, real client configures, focus, output resize and teardown."""

import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import time


def wait_for(needle, log, server, clients, seconds=8, after=0, regex=False):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        log.seek(0)
        output = log.read()
        match = re.search(needle, output[after:]) if regex else needle in output[after:]
        if match:
            return match
        if server.poll() is not None:
            raise RuntimeError(f"Compositor exited while waiting for {needle!r}:\n{output}")
        for client in clients:
            if client.poll() is not None:
                raise RuntimeError(f"Foot exited while waiting for {needle!r}:\n{output}")
        time.sleep(0.05)
    raise RuntimeError(f"Timed out waiting for {needle!r}:\n{output}")


def main():
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if not runtime or not os.environ.get("WAYLAND_DISPLAY"):
        raise RuntimeError("Run this test from a Wayland desktop session")

    socket = f"flowstate-tiles-{os.getpid()}"
    socket_path = Path(runtime) / socket
    with tempfile.TemporaryFile(mode="w+t") as log:
        server = subprocess.Popen([sys.argv[1], socket], stdout=log,
                                  stderr=subprocess.STDOUT)
        clients = []
        cleanup_error = None
        try:
            deadline = time.monotonic() + 8
            while not socket_path.exists() and time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError("Compositor exited before creating its socket")
                time.sleep(0.05)
            if not socket_path.exists():
                raise RuntimeError("Compositor did not create its socket")

            env = {**os.environ, "WAYLAND_DISPLAY": socket}
            for number in range(1, 4):
                options = ["--maximized"] if number == 2 else []
                clients.append(subprocess.Popen(
                    [sys.argv[2], "-o", "csd.preferred=client",
                     "--app-id", f"flowstate-test-{number}", *options,
                     "sh", "-c", "sleep 30"], env=env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                ))
                wait_for(f"Wayland window mapped id={number} app=flowstate-test-{number}",
                         log, server, clients)

            wait_for("tile id=1 x=12 y=12 width=462 height=576", log, server, clients)
            wait_for("tile id=2 x=486 y=12 width=462 height=282", log, server, clients)
            wait_for("tile id=3 x=486 y=306 width=462 height=282", log, server, clients)
            for number, width, height in ((1, 462, 576), (2, 462, 282), (3, 462, 282)):
                match = wait_for(
                    rf"geometry id={number} offset=(-?\d+),(-?\d+) width={width} height={height}",
                    log, server, clients, regex=True)
                if match.group(1) == "0" and match.group(2) == "0":
                    raise RuntimeError("Foot did not exercise an xdg geometry offset")
            wait_for("focus id=3", log, server, clients)
            wait_for("activation id=3 active=1", log, server, clients)
            wait_for("Vulkan frame with client committed", log, server, clients)

            log.seek(0)
            mark = len(log.read())
            server.send_signal(signal.SIGUSR2)
            hover = wait_for(r"hover target id=(\d+)", log, server, clients,
                             after=mark, regex=True)
            hovered = int(hover.group(1))
            wait_for(f"focus id={hovered} keyboard=1", log, server, clients, after=mark)
            wait_for(f"activation id={hovered} active=1", log, server, clients, after=mark)
            for _ in range(3):
                log.seek(0)
                mark = len(log.read())
                server.send_signal(signal.SIGRTMIN)
                cycle = wait_for(r"focus cycled from id=(\d+) to id=(\d+)",
                                 log, server, clients, after=mark, regex=True)
                source, target = map(int, cycle.groups())
                if source not in (1, 2, 3) or target not in (1, 2, 3) or source == target:
                    raise RuntimeError(f"Invalid focus cycle {source} -> {target}")
                wait_for(f"focus id={target} keyboard=1", log, server, clients,
                         after=mark)
            server.send_signal(signal.SIGUSR1)
            wait_for("output resized 800x500", log, server, clients)
            wait_for("tile id=1 x=12 y=12 width=382 height=476", log, server, clients)
            wait_for("tile id=2 x=406 y=12 width=382 height=232", log, server, clients)
            wait_for("tile id=3 x=406 y=256 width=382 height=232", log, server, clients)
            for number, width, height in ((1, 382, 476), (2, 382, 232), (3, 382, 232)):
                wait_for(
                    rf"geometry id={number} offset=(-?\d+),(-?\d+) width={width} height={height}",
                    log, server, clients, regex=True)

            clients[0].terminate()
            clients[0].wait(timeout=5)
            clients.pop(0)
            wait_for("Wayland window unmapped id=1", log, server, clients)
            wait_for("tile id=2 x=12 y=12 width=382 height=476", log, server, clients)

            log.seek(0)
            mark = len(log.read())
            server.send_signal(signal.SIGUSR1)
            wait_for("output resized 960x600", log, server, clients, after=mark)
            wait_for("tile id=2 x=12 y=12 width=462 height=576",
                     log, server, clients, after=mark)
            wait_for("tile id=3 x=486 y=12 width=462 height=576",
                     log, server, clients, after=mark)
            for number in (2, 3):
                wait_for(rf"geometry id={number} offset=(-?\d+),(-?\d+) width=462 height=576",
                         log, server, clients, after=mark, regex=True)

            if "--stress" in sys.argv[3:]:
                for iteration in range(8):
                    log.seek(0)
                    mark = len(log.read())
                    server.send_signal(signal.SIGUSR1)
                    expected = "800x500" if iteration % 2 == 0 else "960x600"
                    wait_for(f"output resized {expected}", log, server, clients, after=mark)
                    log.seek(0)
                    mark = len(log.read())
                    server.send_signal(signal.SIGRTMIN)
                    wait_for(r"focus cycled from id=\d+ to id=\d+", log, server,
                             clients, after=mark, regex=True)
        finally:
            for client in clients:
                if client.poll() is None:
                    client.terminate()
                try:
                    client.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    client.kill()
                    client.wait()
            if server.poll() is None:
                try:
                    wait_for("Wayland window unmapped id=2", log, server, [])
                    wait_for("Wayland window unmapped id=3", log, server, [])
                except RuntimeError as error:
                    cleanup_error = error
            if server.poll() is None:
                server.terminate()
            server.wait(timeout=5)

        log.seek(0)
        output = log.read()
        if cleanup_error:
            raise cleanup_error
        if server.returncode or "allocations left" in output:
            raise RuntimeError(f"Compositor did not clean up:\n{output}")
        if socket_path.exists():
            raise RuntimeError("Compositor left its Wayland socket behind")
    print("Three foot clients tiled, resized, focused, reflowed and cleaned up")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
