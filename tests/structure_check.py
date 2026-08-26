#!/usr/bin/env python3
"""检查 WCH-Link 私有边界，默认只报告，--strict 时阻断。"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = ROOT / "src"
WCHLINK_SOURCE_ROOT = SOURCE_ROOT / "wchlink"
ALLOWLIST = ROOT / "tests" / "macro_allowlist.txt"
XMAKE = ROOT / "xmake.lua"

WCHLINK_HEADER_NAMES = {
    path.name for path in WCHLINK_SOURCE_ROOT.rglob("*.h") if path.is_file()
}
WCHLINK_PRIVATE_INCLUDE_DIRS = {
    "src/wchlink/flash",
    "src/wchlink/protocol",
    "src/wchlink/rvswd",
    "src/wchlink/session",
    "src/wchlink/target",
    "src/wchlink/transport",
    "src/wchlink/usb",
}

IMPLICIT_ERROR_SYMBOLS = {
    "rvswd_flash_last_error",
    "rvswd_memory_failure_abstractcs",
    "rvswd_memory_failure_address",
    "rvswd_memory_failure_dmi_status",
    "rvswd_memory_last_error",
    "rvswd_transport_failure_retryable",
    "rvswd_transport_last_status",
}

REMOVED_TARGET_SYMBOLS = {
    "rvswd_target_session",
    "wchlink_target_ports_chip_id",
    "wchlink_target_ports_family",
    "wchlink_target_ports_is_connected",
    "wchlink_target_ports_supports_memory_streaming",
    "wchlink_target_ports_uses_ch5xx_loader",
    "wchlink_target_ports_uses_l103_loader",
}

FLASH_BACKEND_HEADER_CONSUMERS = {
    "wchlink/flash/rvswd_flash_ch32.h": {
        "src/wchlink/flash/rvswd_flash.c",
        "src/wchlink/flash/rvswd_flash_ch32.c",
    },
    "wchlink/flash/rvswd_flash_ch5xx.h": {
        "src/wchlink/flash/rvswd_flash.c",
        "src/wchlink/flash/rvswd_flash_ch5xx.c",
    },
}

FLASH_INTERNAL_HEADER_CONSUMERS = {
    "wchlink/flash/rvswd_flash_ch32_internal.h": {
        "src/wchlink/flash/rvswd_flash_ch32.c",
        "src/wchlink/flash/rvswd_flash_option.c",
    },
}

TARGET_INTERNAL_HEADER_CONSUMERS = {
    "wchlink/target/wchlink_target_ports_internal.h": {
        "src/wchlink/session/wchlink_session.c",
        "src/wchlink/target/rvswd_target_connect.c",
        "src/wchlink/target/wchlink_target_ports.c",
    },
}


def source_files() -> list[Path]:
    return sorted(
        path
        for path in SOURCE_ROOT.rglob("*")
        if path.suffix in {".c", ".h", ".S"} and path.is_file()
    )


def include_files() -> list[Path]:
    return source_files() + sorted(
        path
        for path in (ROOT / "tests").rglob("*")
        if path.suffix in {".c", ".h"} and path.is_file()
    )


def allowed_macros() -> set[str]:
    if not ALLOWLIST.exists():
        return set()
    return {
        line.strip()
        for line in ALLOWLIST.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def find_violations() -> list[str]:
    violations: list[str] = []
    files = source_files()
    quoted_include_pattern = re.compile(r'^\s*#include\s+"([^"]+)"', re.MULTILINE)

    for path in include_files():
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        for include in quoted_include_pattern.findall(text):
            if include in WCHLINK_HEADER_NAMES:
                violations.append(
                    f"WCH-Link header 未使用限定路径 {include}: {relative}"
                )

    for path in files:
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        if "rvswd_gpio" in text:
            violations.append(f"旧 facade 符号仍存在: {relative}")
        if relative.as_posix().startswith("src/wchlink/") and "SIZE_MAX" in text:
            violations.append(f"WCH-Link 状态仍使用 SIZE_MAX 哨兵: {relative}")
        if "wchlink_session_take_isp_request" in text:
            violations.append(f"session 仍暴露 IAP pending getter: {relative}")
        for symbol in sorted(IMPLICIT_ERROR_SYMBOLS):
            if symbol in text:
                violations.append(f"隐式错误状态符号仍存在 {symbol}: {relative}")
        for symbol in sorted(REMOVED_TARGET_SYMBOLS):
            if symbol in text:
                violations.append(f"已删除 target 浅层符号仍存在 {symbol}: {relative}")

        if relative.parts[:3] in {
            ("src", "main.c"),
        }:
            pass

        if relative.as_posix().startswith(("src/wchlink/protocol/", "src/wchlink/usb/")):
            if re.search(r'#include\s+["<](?:rvswd_|target/)', text):
                violations.append(f"公共层包含 target/transport 私有 header: {relative}")
        if relative.as_posix().startswith("src/wchlink/usb/"):
            if re.search(r"\bWCHLINK_(?:FAMILY|CONTROL)_", text):
                violations.append(f"USB adapter 仍解释 WCH-Link wire command: {relative}")
        for header, consumers in FLASH_BACKEND_HEADER_CONSUMERS.items():
            if header in text and relative.as_posix() not in consumers:
                violations.append(f"调用者绕过 Flash facade: {relative}")
        for header, consumers in FLASH_INTERNAL_HEADER_CONSUMERS.items():
            if header in text and relative.as_posix() not in consumers:
                violations.append(f"调用者绕过 Flash 私有 seam: {relative}")
        for header, consumers in TARGET_INTERNAL_HEADER_CONSUMERS.items():
            if header in text and relative.as_posix() not in consumers:
                violations.append(f"调用者读取 target 私有存储: {relative}")
        if relative.as_posix().startswith("src/wchlink/flash/"):
            if re.search(r'#include\s+["<]wchlink/(?:session|protocol|usb)/', text):
                violations.append(f"Flash backend 反向依赖上层模块: {relative}")

    xmake_text = XMAKE.read_text(encoding="utf-8")
    include_dir_calls = re.findall(r"add_includedirs\((.*?)\)", xmake_text, re.DOTALL)
    for call in include_dir_calls:
        for include_dir in sorted(WCHLINK_PRIVATE_INCLUDE_DIRS):
            if include_dir in call:
                violations.append(
                    f"xmake 全局暴露 WCH-Link 私有 include path: {include_dir}"
                )

    definitions: dict[str, list[str]] = defaultdict(list)
    define_pattern = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)")
    for path in files:
        relative = path.relative_to(ROOT)
        for line in path.read_text(encoding="utf-8").splitlines():
            match = define_pattern.match(line)
            if match:
                definitions[match.group(1)].append(relative.as_posix())

    allowlist = allowed_macros()
    for name, locations in sorted(definitions.items()):
        if len(locations) > 1 and name not in allowlist:
            violations.append(
                f"重复工程宏 {name}: {', '.join(locations)}"
            )

    return violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    violations = find_violations()
    if violations:
        for violation in violations:
            print(f"WARN: {violation}")
        return 1 if args.strict else 0
    print("structure check: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
