#!/usr/bin/env python3
"""Run the production folder mover on temporary files, with I/O failures.

Only the three fault-injected calls are substituted. Keeping the mover bodies
in the generated include makes this test exercise porpoise_main.cpp without
requiring the PS5 graphics, controller, or SDK dependencies.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


root = Path(__file__).resolve().parents[2]
source = (root / "src/porpoise_main.cpp").read_text()
start = source.index("struct Mover\n")
end = source.index("void draw_mover(", start)
body = source[start:end]
copy_start = body.index("bool copy_into(")
copy_end = body.index("void remove_tree(", copy_start)
copy = body[copy_start:copy_end]
for original, replacement in (
    ("std::fread(", "fault_fread("),
    ("stat(src.c_str(), &st)", "fault_stat(src.c_str(), &st)"),
    ("readdir(d)", "fault_readdir(d)"),
):
    assert copy.count(original) == 1, "The mover changed: update the fault injection point"
    copy = copy.replace(original, replacement)
body = body[:copy_start] + copy + body[copy_end:]

with tempfile.TemporaryDirectory(prefix="porpoise-mover-") as temporary:
    directory = Path(temporary)
    (directory / "mover.inc").write_text(body)
    executable = directory / "mover-test"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    flags = shlex.split(os.environ.get("CXXFLAGS", ""))
    subprocess.run(
        compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror"] + flags
        + ["-I" + str(root / "src"), "-I" + str(directory),
           str(root / "tests/mover/test.cpp"), "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable), str(directory / "fixtures")], check=True)
