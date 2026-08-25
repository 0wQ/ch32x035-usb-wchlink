local function getenv_first(...)
    for _, name in ipairs({...}) do
        local value = os.getenv(name)
        if value and #value > 0 then
            return path.translate(value)
        end
    end
end

local function first_existing_tool_prefix(candidates, toolname)
    for _, prefix in ipairs(candidates) do
        if os.isfile(prefix .. toolname) then
            return prefix
        end
    end
    return nil
end

local function append_unique(values, item)
    if not item or #item == 0 then
        return
    end
    for _, value in ipairs(values) do
        if value == item then
            return
        end
    end
    table.insert(values, item)
end

local function resolve_toolchain_root(root)
    local toolchain_root = path.translate(root)
    local toolchain_bin = path.join(toolchain_root, "bin")
    local gcc_prefix = first_existing_tool_prefix({
        path.join(toolchain_bin, "riscv32-wch-elf-"),
        path.join(toolchain_bin, "riscv-wch-elf-"),
        path.join(toolchain_bin, "riscv-none-embed-")
    }, "gcc")
    if gcc_prefix then
        return toolchain_root, toolchain_bin, gcc_prefix
    end
end

local function find_toolchain_from_env()
    local root = getenv_first("WCH_TOOLCHAIN_ROOT")
    if root then
        return resolve_toolchain_root(root)
    end
end

local function host_toolchain_root_candidates()
    local roots = {}

    if os.host() == "macosx" then
        append_unique(roots, "/Users/sora/wch/Toolchain/RISC-V Embedded GCC12")
        append_unique(roots, "/Applications/MounRiver Studio 2.app/Contents/Resources/app/resources/darwin/components/WCH/Toolchain/RISC-V Embedded GCC12")
        append_unique(roots, "/Applications/MounRiver Studio 2.app/Contents/Resources/app/resources/darwin/components/WCH/Toolchain/RISC-V Embedded GCC")
    elseif os.host() == "windows" then
        append_unique(roots, "C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC12")
        append_unique(roots, "C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC")
    end

    return roots
end

local function find_toolchain_from_host()
    for _, root in ipairs(host_toolchain_root_candidates()) do
        local toolchain_root, toolchain_bin, gcc_prefix = resolve_toolchain_root(root)
        if gcc_prefix then
            return toolchain_root, toolchain_bin, gcc_prefix
        end
    end
end

local function find_toolchain()
    local toolchain_root, toolchain_bin, gcc_prefix = find_toolchain_from_env()
    if not gcc_prefix then
        toolchain_root, toolchain_bin, gcc_prefix = find_toolchain_from_host()
    end
    return toolchain_root, toolchain_bin, gcc_prefix
end

toolchain("wch-riscv")
    set_kind("standalone")
    set_homepage("https://www.wch.cn/")
    set_description("WCH RISC-V embedded GCC toolchain")
    on_check(function ()
        return true
    end)

    on_load(function (toolchain)
        local toolchain_root, _, gcc_prefix = find_toolchain()
        if not gcc_prefix then
            raise("WCH toolchain not found; set WCH_TOOLCHAIN_ROOT or install MounRiver Studio 2 in the default host path")
        end

        toolchain:set("plat", "cross")
        toolchain:set("arch", "riscv")
        toolchain:set("toolset", "cc", gcc_prefix .. "gcc")
        toolchain:set("toolset", "cxx", gcc_prefix .. "g++")
        toolchain:set("toolset", "as", gcc_prefix .. "gcc")
        toolchain:set("toolset", "ld", gcc_prefix .. "gcc")
        toolchain:set("toolset", "sh", gcc_prefix .. "gcc")
        toolchain:set("toolset", "ar", gcc_prefix .. "ar")
        toolchain:set("toolset", "ranlib", gcc_prefix .. "ranlib")
        toolchain:set("toolset", "strip", gcc_prefix .. "strip")
        toolchain:set("toolset", "objcopy", gcc_prefix .. "objcopy")
        toolchain:set("toolchain_root", toolchain_root)
        toolchain:set("gcc_prefix", gcc_prefix)
    end)
toolchain_end()
