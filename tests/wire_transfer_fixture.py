#!/usr/bin/env python3
"""运行 WCH-Link wire、profile 和内存 target 主机 fixture。"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile


def run_wire_fixture() -> None:
    project = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CC", "cc")

    with tempfile.TemporaryDirectory(prefix="wchlink-wire-") as temp_dir:
        executable = Path(temp_dir) / "wire_fixture"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(project / "src"),
                str(project / "tests/wire_fixture.c"),
                str(project / "src/wchlink/protocol/wchlink_wire.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def run_command_transfer_fixture() -> None:
    project = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CC", "cc")

    with tempfile.TemporaryDirectory(
        prefix="wchlink-command-transfer-"
    ) as temp_dir:
        executable = Path(temp_dir) / "command_transfer_fixture"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(project / "src"),
                "-I",
                str(project / "tests"),
                str(project / "tests/command_transfer_fixture.c"),
                str(project / "tests/in_memory_target.c"),
                str(project / "src/wchlink/session/wchlink_command.c"),
                str(project / "src/wchlink/session/wchlink_command_target.c"),
                str(project / "src/wchlink/session/wchlink_direct_dmi_resume.c"),
                str(project / "src/wchlink/session/wchlink_command_transfer.c"),
                str(project / "src/wchlink/session/wchlink_transfer.c"),
                str(project / "src/wchlink/protocol/wchlink_wire.c"),
                str(project / "src/wchlink/target/rvswd_target_result.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def run_profile_fixture() -> None:
    project = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CC", "cc")

    with tempfile.TemporaryDirectory(prefix="wchlink-profile-") as temp_dir:
        executable = Path(temp_dir) / "profile_fixture"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(project / "src"),
                str(project / "tests/profile_fixture.c"),
                str(project / "src/wchlink/target/rvswd_target_profile.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def run_execute_prepare_fixture() -> None:
    project = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CC", "cc")

    with tempfile.TemporaryDirectory(
        prefix="rvswd-execute-prepare-"
    ) as temp_dir:
        executable = Path(temp_dir) / "rvswd_execute_prepare_fixture"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(project / "src"),
                str(project / "tests/rvswd_execute_prepare_fixture.c"),
                str(project / "src/wchlink/rvswd/rvswd_execute_prepare.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def run_rvswd_debug_resume_fixture() -> None:
    project = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CC", "cc")

    with tempfile.TemporaryDirectory(prefix="rvswd-debug-resume-") as temp_dir:
        executable = Path(temp_dir) / "rvswd_debug_resume_fixture"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(project / "src"),
                str(project / "tests/rvswd_debug_resume_fixture.c"),
                str(project / "src/wchlink/rvswd/rvswd_debug.c"),
                str(project / "src/wchlink/target/rvswd_target_loader.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def run_rvswd_memory_fixture() -> None:
    project = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CC", "cc")

    with tempfile.TemporaryDirectory(
        prefix="rvswd-memory-"
    ) as temp_dir:
        executable = Path(temp_dir) / "rvswd_memory_fixture"
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(project / "src"),
                str(project / "tests/rvswd_memory_fixture.c"),
                str(project / "src/wchlink/rvswd/rvswd_memory.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def run() -> None:
    run_wire_fixture()
    run_command_transfer_fixture()
    run_profile_fixture()
    run_execute_prepare_fixture()
    run_rvswd_debug_resume_fixture()
    run_rvswd_memory_fixture()
    print("wire/command/transfer/profile/debug fixture: pass")


if __name__ == "__main__":
    run()
