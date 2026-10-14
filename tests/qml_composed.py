"""Check real Vulkan QML pixels, tiling with two clients, input and crash cleanup."""

import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time


def wait_for(log, text, processes, timeout=8, after=0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        log.seek(0)
        output = log.read()
        if text in output[after:]:
            return output
        for process in processes:
            if process.poll() is not None:
                raise RuntimeError(f"Process exited before {text!r}:\n{output}")
        time.sleep(0.05)
    raise RuntimeError(f"Timed out waiting for {text!r}:\n{output}")


def main():
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if not runtime or not os.environ.get("WAYLAND_DISPLAY"):
        raise RuntimeError("Run this test from a Wayland desktop session")
    socket = f"flowstate-qml-{os.getpid()}"
    socket_path = Path(runtime) / socket
    with tempfile.TemporaryDirectory() as directory, \
         tempfile.TemporaryFile(mode="w+t") as server_log, \
         tempfile.TemporaryFile(mode="w+t") as qml_log:
        server = subprocess.Popen([sys.argv[1], socket], stdout=server_log,
                                  stderr=subprocess.STDOUT)
        clients = []
        try:
            deadline = time.monotonic() + 8
            while not socket_path.exists() and time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError("Compositor exited before socket creation")
                time.sleep(0.05)
            if not socket_path.exists():
                raise RuntimeError("Compositor did not create its socket")

            env = {**os.environ, "WAYLAND_DISPLAY": socket, "QT_QPA_PLATFORM": "wayland"}
            image_path = Path(directory) / "qml.png"
            qml = subprocess.Popen([sys.argv[2], "--visual-report", str(image_path)],
                                   env=env, stdout=qml_log, stderr=subprocess.STDOUT)
            clients.append(qml)
            wait_for(server_log, "Wayland window mapped id=1", [server, qml])
            wait_for(qml_log, "QML Vulkan pixels verified", [server, qml])
            if not image_path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n"):
                raise RuntimeError("Qt did not save a PNG of rendered QML pixels")

            for number in (2, 3):
                foot = subprocess.Popen(
                    [sys.argv[3], "--app-id", f"flowstate-mixed-{number}",
                     "sh", "-c", "sleep 30"], env=env,
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                clients.append(foot)
                wait_for(server_log, f"Wayland window mapped id={number} app=flowstate-mixed-{number}",
                         [server, *clients])
            wait_for(server_log, "tile id=1 x=12 y=12 width=462 height=576", [server, *clients])
            wait_for(server_log, "tile id=2 x=486 y=12 width=462 height=282", [server, *clients])
            wait_for(server_log, "tile id=3 x=486 y=306 width=462 height=282", [server, *clients])
            wait_for(server_log, "Vulkan frame with client committed", [server, *clients])

            # Warp the pointer into the QML tile to keep hover focus there.
            # The next signal injects A through the normal keyboard seat path.
            server_log.seek(0)
            mark = len(server_log.read())
            server.send_signal(signal.SIGUSR2)
            wait_for(server_log, "focus id=1 keyboard=1", [server, *clients], after=mark)
            server.send_signal(signal.SIGRTMIN + 1)
            try:
                wait_for(qml_log, "Qt window received key=65", [server, *clients])
            except RuntimeError as error:
                server_log.seek(0)
                raise RuntimeError(f"{error}\nCompositor log:\n{server_log.read()}") from error

            clients[-1].kill()  # Abrupt client death must still unmap and reflow.
            clients[-1].wait(timeout=5)
            clients.pop()
            wait_for(server_log, "Wayland window unmapped id=3", [server, *clients])
            wait_for(server_log, "tile id=2 x=486 y=12 width=462 height=576", [server, *clients])
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
                server.terminate()
            server.wait(timeout=5)

        server_log.seek(0)
        output = server_log.read()
        if server.returncode or "allocations left" in output:
            raise RuntimeError(f"Compositor did not clean up:\n{output}")
        if socket_path.exists():
            raise RuntimeError("Compositor left its Wayland socket behind")
    print("Vulkan QML pixels, three tiled clients, key delivery and crash cleanup verified")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
