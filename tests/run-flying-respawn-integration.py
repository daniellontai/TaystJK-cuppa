#!/usr/bin/env python3
"""Run frame-Force/timer regressions against the actual Linux game module objects."""
import argparse
import os
import platform
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build_directory", type=Path)
parser.add_argument("--target", default="jampgamex86_64")
parser.add_argument("--finish", action="store_true", help="Run only completion, persistence and help cases")
parser.add_argument("--case", help="Run one named integration case")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
build = args.build_directory.resolve()
subprocess.run(["cmake", "--build", str(build), "--target", args.target, "-j2"], check=True)
objects = sorted((build / "codemp/game/CMakeFiles" / (args.target + ".dir")).rglob("*.o"))
if not objects:
    raise SystemExit("No compiled game objects found; this runner requires a Linux Makefile build.")
probe_object = build / "FlyingRespawnIntegrationTests.o"
executable = build / "FlyingRespawnIntegrationTests"
subprocess.run([
    os.environ.get("CC", "cc"), "-D_GAME", "-DFINAL_BUILD", "-fcommon",
    '-DARCH_STRING="' + platform.machine() + '"',
    "-I" + str(root / "codemp"), "-I" + str(root / "codemp/game"),
    "-I" + str(root / "shared"), "-I" + str(build / "shared"),
    "-I" + str(root / "lib/gsl-lite/include"),
    "-c", str(root / "tests/FlyingRespawnIntegrationTests.c"), "-o", str(probe_object),
], check=True)
subprocess.run([
    os.environ.get("CXX", "c++"), str(probe_object), *map(str, objects),
    "-ldl", "-lpthread", "-lm", "-o", str(executable),
], check=True)
with tempfile.TemporaryDirectory(prefix="flying-respawn-integration-") as working:
    subprocess.run([str(executable), *(["--case", args.case] if args.case else ["finish"] if args.finish else [])], cwd=working, check=True)
