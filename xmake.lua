set_project("ch32x035-usb-wchlink")
set_version("0.1.0")
set_xmakever("2.9.8")
set_policy("build.release.strip", false)

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})
includes("toolchains/wch-riscv/xmake.lua")

-- 解析 Berkeley size 输出，提取 text/data/bss
local function parse_berkeley_size(size_output)
    for line in size_output:gmatch("[^\r\n]+") do
        local text, data, bss = line:match("^%s*(%d+)%s+(%d+)%s+(%d+)%s+%d+%s+%x+%s+")
        if text and data and bss then
            return {
                text = tonumber(text),
                data = tonumber(data),
                bss = tonumber(bss)
            }
        end
    end
    raise("failed to parse berkeley size output")
end

-- Flash = text + data, RAM = data + bss
local function print_memory_summary(size_info)
    local flash_used = size_info.text + size_info.data
    local ram_used = size_info.data + size_info.bss
    local function fmt(b) if b >= 1024 then return string.format("%.2f KiB (%d bytes)", b / 1024.0, b) end; return string.format("%d bytes", b) end
    print(string.format("Flash used: %s", fmt(flash_used)))
    print(string.format("RAM   used: %s", fmt(ram_used)))
end

local arch_flags = {"-march=rv32imacxw", "-mabi=ilp32", "-msmall-data-limit=8", "-mno-save-restore"}

target("firmware")
    set_plat("cross")
    set_arch("riscv")
    set_kind("binary")
    set_targetdir("build/$(mode)")
    set_filename("firmware.elf")
    set_languages("gnu11")
    set_toolchains("wch-riscv")
    set_warnings("all")

    add_files("src/**.c", "src/**.S")
    add_files("sdk/Core/*.c", "sdk/Peripheral/src/*.c", "sdk/System/*.c", "sdk/Startup/startup_ch32x035_highcode.S")
    add_files("third_party/cherryusb/core/usbd_core.c", "third_party/cherryusb/class/cdc/usbd_cdc_acm.c", "third_party/cherryusb_port/usb_ch32x035_dc_usbfs.c")
    add_includedirs("src", "sdk/Core", "sdk/Peripheral/inc", "sdk/System")
    add_sysincludedirs("third_party/cherryusb/core", "third_party/cherryusb/common", "third_party/cherryusb/class/cdc", "third_party/cherryusb_port")

    add_cxflags(arch_flags, "-D__PACKED=__attribute__((packed))", "-fmessage-length=0", "-fsigned-char", "-ffunction-sections", "-fdata-sections", "-fno-common", "-Wno-comment", "-Wno-unused-parameter", "-Wno-missing-prototypes", {force = true})
    add_asflags(arch_flags, "-ffunction-sections", "-fdata-sections", {force = true})
    add_ldflags(arch_flags, "-ffunction-sections", "-fdata-sections", "--specs=nano.specs", "--specs=nosys.specs", "-nostartfiles", "-Wl,-Tsdk/Ld/Link_highcode_nv_256B.ld", "-Wl,--gc-sections", {force = true})

    if is_mode("debug") then
        set_symbols("debug")
        set_optimize("none")
        add_cxflags("-Og", {force = true})
    else
        set_symbols("hidden")
        set_optimize("smallest")
        add_cxflags("-flto", {force = true})
        add_ldflags("-flto", {force = true})
    end

    before_build(function (target)
        -- 构建前打印工具链和 SDK
        local toolchain = assert(target:toolchain("wch-riscv"))
        local toolchain_root = toolchain:get("toolchain_root")
        cprint("${cyan}Using mode:${clear} %s", get_config("mode") or "debug")
        cprint("${cyan}Using toolchain:${clear} %s", toolchain_root)
        cprint("${cyan}Using SDK:${clear} sdk/")
    end)

    before_link(function (target)
        -- map 文件跟随输出目录生成
        local mapfile = path.join(target:targetdir(), target:basename() .. ".map")
        target:add("ldflags", "-Wl,-Map=" .. mapfile, {force = true})
    end)

    after_build(function (target)
        local toolchain = assert(target:toolchain("wch-riscv"))
        local gcc_prefix = toolchain:get("gcc_prefix")
        local objcopy = gcc_prefix .. "objcopy"
        local size = gcc_prefix .. "size"
        local strip = gcc_prefix .. "strip"
        local targetfile = target:targetfile()
        local bindir = path.directory(targetfile)

        os.execv(objcopy, {"-O", "binary", targetfile, path.join(bindir, "firmware.bin")})

        print(os.iorunv(size, {"--format=sysv", targetfile}))
        print_memory_summary(parse_berkeley_size(os.iorunv(size, {"--format=berkeley", targetfile})))
    end)

    after_clean(function (target)
        local bindir = target:targetdir()
        os.rm(path.join(bindir, "firmware.bin"))
        os.rm(path.join(bindir, target:basename() .. ".map"))
    end)
