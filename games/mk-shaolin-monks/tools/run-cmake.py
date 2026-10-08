"""Run CMake with one Windows Path key, avoiding MSBuild PATH/Path collisions."""
import os
import subprocess
import sys

env = {key: value for key, value in os.environ.items() if key.lower() != "path"}
env["Path"] = os.environ.get("PATH", os.environ.get("Path", ""))
raise SystemExit(subprocess.call(["cmake", *sys.argv[1:]], env=env))
