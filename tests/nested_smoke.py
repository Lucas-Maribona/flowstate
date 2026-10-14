"""Open a nested Vulkan compositor and render one real Wayland client in it."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main() -> int:
    runtime = os.environ.get("XDG_RUNTIME_DIR")
    if not runtime or not os.environ.get("WAYLAND_DISPLAY"):
        raise RuntimeError("Run this test from a Wayland desktop session")

    socket = f"flowstate-smoke-{os.getpid()}"
    socket_path = Path(runtime) / socket
    with tempfile.TemporaryFile(mode="w+t") as log:
        server = subprocess.Popen([sys.argv[1], socket], stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 8
            while not socket_path.exists() and time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError("Nested compositor exited before creating its socket")
                time.sleep(0.05)
            if not socket_path.exists():
                raise RuntimeError("Nested compositor did not create its socket")

            client_env = os.environ.copy()
            client_env["WAYLAND_DISPLAY"] = socket
            client = subprocess.run(
                [sys.argv[2], "--probe-vulkan"], env=client_env,
                capture_output=True, text=True, timeout=10, check=False,
            )
            if client.returncode or "initialized with Vulkan" not in client.stderr:
                raise RuntimeError(f"Wayland client failed:\n{client.stderr}")

            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                log.seek(0)
                output = log.read()
                if "Vulkan frame with client committed" in output:
                    break
                if server.poll() is not None:
                    raise RuntimeError("Nested compositor exited while rendering the client")
                time.sleep(0.05)
            else:
                raise RuntimeError(f"No client frame committed:\n{output}")
        finally:
            if server.poll() is None:
                server.terminate()
            server.wait(timeout=5)

        if server.returncode:
            log.seek(0)
            raise RuntimeError(f"Nested compositor failed to exit cleanly:\n{log.read()}")
        if socket_path.exists():
            raise RuntimeError("Nested compositor left its Wayland socket behind")
        log.seek(0)
        output = log.read()
        if "allocations left" in output:
            raise RuntimeError("Vulkan allocations remained after shutdown")
        if "Validation Error" in output:
            raise RuntimeError(f"Vulkan validation reported an error:\n{output}")
    print("Nested Qt Wayland client mapped and a Vulkan frame was committed")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
