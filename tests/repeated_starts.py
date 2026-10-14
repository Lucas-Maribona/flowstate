"""Repeat the existing nested smoke test to catch teardown/startup regressions."""

import subprocess
import sys


for run in range(1, 5):
    result = subprocess.run(
        [sys.executable, *sys.argv[1:]], capture_output=True, text=True,
        timeout=20, check=False,
    )
    if result.returncode:
        print(f"Nested compositor start {run} failed:\n{result.stdout}{result.stderr}",
              file=sys.stderr)
        sys.exit(result.returncode)
print("Four consecutive Vulkan compositor starts and stops passed")
