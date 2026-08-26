#!/usr/bin/env python3
"""固定 WCH-Link wire 和 transfer 边界，作为主机侧 golden fixture。"""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile


COMMAND_PREFIX = 0x81
REPLY_PREFIX = 0x82
FLASH_PACKET_SIZE = 256
FLASH_CHUNK_SIZE = 4096
CH5XX_PAGE_SIZE = 256


def ack(family: int) -> bytes:
    return bytes((REPLY_PREFIX, family, 1, 0))


def unsupported(family: int) -> bytes:
    return bytes((COMMAND_PREFIX, family, 1, 2))


def checksum_add(checksum: int, data: bytes) -> int:
    assert len(data) % 4 == 0
    for offset in range(0, len(data), 4):
        checksum = (checksum + int.from_bytes(data[offset : offset + 4], "little")) & 0xFFFFFFFF
    return checksum


def ch5xx_padded_length(length: int) -> int:
    return (length + CH5XX_PAGE_SIZE - 1) & ~(CH5XX_PAGE_SIZE - 1)


def chunk_lengths(total: int) -> list[int]:
    result: list[int] = []
    while total:
        length = min(total, FLASH_CHUNK_SIZE)
        result.append(length)
        total -= length
    return result


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
                str(project / "src/wchlink/protocol"),
                str(project / "tests/wire_fixture.c"),
                str(project / "src/wchlink/protocol/wchlink_wire.c"),
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
                str(project / "src/wchlink/target"),
                "-I",
                str(project / "src/wchlink/rvswd"),
                "-I",
                str(project / "src/wchlink/protocol"),
                str(project / "tests/profile_fixture.c"),
                str(project / "src/wchlink/target/rvswd_target_profile.c"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        subprocess.run([str(executable)], check=True)


def run() -> None:
    run_wire_fixture()
    run_profile_fixture()
    assert ack(0x0D) == bytes.fromhex("82 0d 01 00")
    assert unsupported(0x02) == bytes.fromhex("81 02 01 02")
    assert checksum_add(0, bytes.fromhex("01 00 00 00 ff ff ff ff")) == 0x100000000 & 0xFFFFFFFF
    assert ch5xx_padded_length(0) == 0
    assert ch5xx_padded_length(1) == FLASH_PACKET_SIZE
    assert ch5xx_padded_length(FLASH_PACKET_SIZE) == FLASH_PACKET_SIZE
    assert ch5xx_padded_length(FLASH_PACKET_SIZE + 1) == FLASH_PACKET_SIZE * 2
    assert chunk_lengths(FLASH_CHUNK_SIZE * 2 + 17) == [FLASH_CHUNK_SIZE, FLASH_CHUNK_SIZE, 17]
    assert chunk_lengths(0) == []
    print("wire/transfer/profile fixture: pass")


if __name__ == "__main__":
    run()
