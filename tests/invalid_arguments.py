"""Invalid compositor options must fail before creating a Wayland socket."""

import subprocess
import sys


binary = sys.argv[1]
for args, expected in [
    (["test-socket", "--gap", "-1"], "Gap must be an integer"),
    (["test-socket", "--gap", "129"], "Gap must be an integer"),
    (["test-socket", "--gap", "nope"], "Gap must be an integer"),
    (["test-socket", "--gap"], "Usage:"),
]:
    result = subprocess.run([binary, *args], capture_output=True, text=True,
                            timeout=3, check=False)
    if result.returncode != 1 or expected not in result.stderr:
        print(f"Bad result for {args}: {result.returncode}\n{result.stderr}", file=sys.stderr)
        sys.exit(1)
print("Invalid compositor options rejected cleanly")
