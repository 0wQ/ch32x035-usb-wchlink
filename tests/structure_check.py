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
}

FLASH_BACKEND_HEADER_CONSUMERS = {
    "wchlink/flash/rvswd_flash_ch32.h": {
        "src/wchlink/flash/rvswd_flash.c",
        "src/wchlink/flash/rvswd_flash_ch32.c",
        "src/wchlink/target/rvswd_target_x03x.c",
        "src/wchlink/target/rvswd_target_l103.c",
        "src/wchlink/target/rvswd_target_v30x.c",
    },
    "wchlink/flash/rvswd_flash_ch58x_59x.h": {
        "src/wchlink/flash/rvswd_flash.c",
        "src/wchlink/flash/rvswd_flash_ch58x_59x.c",
        "src/wchlink/target/rvswd_target_ch58x.c",
        "src/wchlink/target/rvswd_target_ch59x.c",
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

TRANSFER_INTERNAL_HEADER_CONSUMERS = {
    "src/wchlink/session/wchlink_session.c",
    "src/wchlink/session/wchlink_transfer.c",
    "tests/command_transfer_fixture.c",
}

TARGET_PORT_HEADER_CONSUMERS = {
    "wchlink/target/wchlink_target_control.h": {
        "src/wchlink/session/wchlink_command_target.c",
        "src/wchlink/session/wchlink_command_transfer.c",
        "src/wchlink/session/wchlink_transfer.c",
        "src/wchlink/target/rvswd_target_connect.c",
        "src/wchlink/target/wchlink_target_ports.c",
    },
    "wchlink/target/wchlink_target_dmi.h": {
        "src/wchlink/session/wchlink_command_target.c",
        "src/wchlink/target/wchlink_target_ports.c",
    },
    "wchlink/target/wchlink_target_flash.h": {
        "src/wchlink/session/wchlink_command_target.c",
        "src/wchlink/session/wchlink_command_transfer.c",
        "src/wchlink/session/wchlink_transfer.c",
        "src/wchlink/target/wchlink_target_ports.c",
    },
    "wchlink/target/wchlink_target_transfer.h": {
        "src/wchlink/session/wchlink_transfer.c",
        "src/wchlink/target/wchlink_target_ports.c",
    },
}

USB_CALLBACK_FORBIDDEN_CALLS = {
    "wchlink_arm_request": r"\bwchlink_arm_request\s*\(",
    "wchlink_cdc_arm_read": r"\bwchlink_cdc_arm_read\s*\(",
    "wchlink_session_*": r"\bwchlink_session_[A-Za-z0-9_]+\s*\(",
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


def allowed_wchlink_macros() -> dict[str, str]:
    if not ALLOWLIST.exists():
        return {}

    entries: dict[str, str] = {}
    for raw_line in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(maxsplit=1)
        entries[parts[0]] = parts[1].strip() if len(parts) == 2 else ""
    return entries


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
        if (
            "wchlink/session/wchlink_transfer_internal.h" in text
            and relative.as_posix() not in TRANSFER_INTERNAL_HEADER_CONSUMERS
        ):
            violations.append(f"调用者读取 transfer 私有存储: {relative}")

    for path in files:
        text = path.read_text(encoding="utf-8")
        relative = path.relative_to(ROOT)
        if relative.as_posix().startswith("src/wchlink/") and not relative.as_posix().startswith(
            ("src/wchlink/target/", "src/wchlink/protocol/")
        ):
            if re.search(
                r"WCHLINK_TARGET_FAMILY_|ch58x_59x_protocol|rvswd_debug_execute",
                text,
            ):
                violations.append(f"公共 WCH-Link 层仍解释目标族行为: {relative}")
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
            callback_pattern = re.compile(
                r"static void \w+(?:_callback|_event_handler)\([^)]*\)\s*\{(.*?)^\}",
                re.MULTILINE | re.DOTALL,
            )
            for callback_body in callback_pattern.findall(text):
                for name, pattern in sorted(USB_CALLBACK_FORBIDDEN_CALLS.items()):
                    if re.search(pattern, callback_body):
                        violations.append(
                            f"USB callback 越过端点状态所有权 {name}: {relative}"
                        )
        for header, consumers in FLASH_BACKEND_HEADER_CONSUMERS.items():
            if header in text and relative.as_posix() not in consumers:
                violations.append(f"调用者绕过 Flash facade: {relative}")
        for header, consumers in FLASH_INTERNAL_HEADER_CONSUMERS.items():
            if header in text and relative.as_posix() not in consumers:
                violations.append(f"调用者绕过 Flash 私有 seam: {relative}")
        for header, consumers in TARGET_INTERNAL_HEADER_CONSUMERS.items():
            if header in text and relative.as_posix() not in consumers:
                violations.append(f"调用者读取 target 私有存储: {relative}")
        for header, consumers in TARGET_PORT_HEADER_CONSUMERS.items():
            if header in text and relative.as_posix() not in consumers:
                violations.append(f"调用者越过 target 私有 port: {relative}")
        if relative.as_posix().startswith("src/wchlink/flash/"):
            if re.search(r'#include\s+["<]wchlink/(?:session|protocol|usb)/', text):
                violations.append(f"Flash backend 反向依赖上层模块: {relative}")

    if (WCHLINK_SOURCE_ROOT / "target/wchlink_target_ports.h").exists():
        violations.append("target umbrella port header 被重新引入")

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

    allowlist = allowed_wchlink_macros()
    for name, reason in sorted(allowlist.items()):
        if not reason:
            violations.append(f"WCH-Link 宏缺少保留理由: {name}")
        if name not in definitions:
            violations.append(f"WCH-Link 宏 allowlist 存在陈旧条目: {name}")

    for name, locations in sorted(definitions.items()):
        if len(locations) > 1:
            violations.append(
                f"重复工程宏 {name}: {', '.join(locations)}"
            )
        if any(location.startswith("src/wchlink/") for location in locations):
            if name not in allowlist:
                violations.append(
                    f"WCH-Link 工程宏未登记保留理由 {name}: "
                    f"{', '.join(locations)}"
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
