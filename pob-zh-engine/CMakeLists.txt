cmake_minimum_required(VERSION 3.15)
project(SimpleGraphic C CXX)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)

if (APPLE)
    enable_language(OBJCXX)
endif ()

if (MSVC)
    add_compile_options("/Zi")
    add_link_options("/DEBUG:FULL")
endif ()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# ANGLE, on macOS only, from a port of our own.
#
# What ANGLE translates POB's drawing calls into on this platform decides
# whether the surface it draws into keeps step with the window; its Metal path
# does, its older one does not (see the macOS notes). That path is a build
# option, off by default, and only present in a newer ANGLE than the one this
# project's package baseline pins.
#
# An overlay port is how that reaches macOS without reaching Windows: it is
# consulted only on this platform, so a Windows build resolves ANGLE exactly as
# it did before this directory existed -- same version, same options, same
# build steps. The newer port also wants Python packages at build time, which
# the baseline's Python cannot install; a Windows build never runs that step
# because it never reads this port.
if (APPLE)
    # Through the cache, and FORCE: vcpkg reads this as a cache entry, and an
    # ordinary variable of the same name is not what it ends up seeing.
    list(APPEND VCPKG_OVERLAY_PORTS "${PROJECT_SOURCE_DIR}/vcpkg-ports-apple")
    list(REMOVE_DUPLICATES VCPKG_OVERLAY_PORTS)
    set(VCPKG_OVERLAY_PORTS "${VCPKG_OVERLAY_PORTS}" CACHE STRING "Overlay ports for vcpkg" FORCE)
endif ()

include(${PROJECT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)

# macOS app bundle.
#
# Every install destination in this file is a relative path ("engine", "Data",
# "Fonts", "."), and the launcher finds its resources relative to its own
# executable. Pointing the install root at Contents/MacOS therefore lays the
# whole tree out correctly inside a bundle without touching a single install
# rule -- the layout the program expects and the layout macOS expects line up.
#
# Configure with -DPOBTOOLS_NO_BUNDLE=ON for a plain directory install instead.
if (APPLE AND NOT POBTOOLS_NO_BUNDLE)
    set(POBTOOLS_BUNDLE_NAME "PobTools.app")
    # The adjusted prefix is written back to the cache, so configuring an
    # existing build tree a second time would append another bundle layer and
    # install into PobTools.app/Contents/MacOS/PobTools.app/Contents/MacOS.
    # Only adjust a prefix that does not already point inside the bundle.
    if (NOT CMAKE_INSTALL_PREFIX MATCHES "/${POBTOOLS_BUNDLE_NAME}/Contents/MacOS/?$")
        set(CMAKE_INSTALL_PREFIX
            "${CMAKE_INSTALL_PREFIX}/${POBTOOLS_BUNDLE_NAME}/Contents/MacOS"
            CACHE PATH "Install root (inside the app bundle)" FORCE)
    endif ()
    message(STATUS "pob-zh: installing into ${CMAKE_INSTALL_PREFIX}")
endif ()

set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "engine")
include(InstallRequiredSystemLibraries)
add_subdirectory(dep/glm)

set(SIMPLEGRAPHIC_SOURCES
    "config.h"
    "dep/stb/stb_image.h"
    "dep/stb/stb_image_resize.h"
    "dep/stb/stb_image_write.h"
    "engine/common.h"
    "engine/common/base64.c"
    "engine/common/base64.h"
    "engine/common/common.cpp"
    "engine/common/console.cpp"
    "engine/common/console.h"
    "engine/common/keylist.h"
    "engine/common/memtrak3.cpp"
    "engine/common/memtrak3.h"
    "engine/common/streams.cpp"
    "engine/common/streams.h"
    "engine/core/core_compress.cpp"
    "engine/core/core_compress.h"
    "engine/core/core_config.cpp"
    "engine/core/core_config.h"
    "engine/core/core_image.cpp"
    "engine/core/core_image.h"
    "engine/core/core_main.cpp"
    "engine/core/core_main.h"
    "engine/core/core_tex_manipulation.cpp"
    "engine/core/core_tex_manipulation.h"
    "engine/core/core_video.cpp"
    "engine/core/core_video.h"
    "engine/render/r_font.cpp"
    "engine/render/r_font.h"
    "engine/render/r_main.cpp"
    "engine/render/r_main.h"
    "engine/render/r_texture.cpp"
    "engine/render/r_texture.h"
    "engine/system/win/sys_local.h"
    "engine/system/win/sys_main.cpp"
    "engine/system/win/sys_opengl.cpp"
    "engine/system/win/sys_video.cpp"
    "engine/system/sys_console.h"
    "engine/system/sys_main.h"
    "engine/system/sys_opengl.h"
    "engine/system/sys_video.h"
    "win/entry.cpp"
    # Headless mode: stdio JSON transport + the Draw* stubs and bridge dispatch
    # that let POB's Lua run with no window for the new UI (host --modern-ui).
    "engine/headless_ipc.h"
    "engine/headless_bridge.cpp"
    "engine/headless_bridge.h"
    "engine/texture_atlas.cpp"
    "engine/texture_atlas.h"
    "ui.h"
    "ui_api.cpp"
    # Shared failure log. Compiled into BOTH targets on purpose: the engine
    # cannot call into the host, and the POB patch failures worth recording
    # happen in ui_api.cpp. Appends are atomic, so two copies is not a race.
    "host/error_log.cpp"
    # Same reasoning for the hang/crash watchdog: POB runs in its own process,
    # so the engine has to watch its own frame loop.
    "host/hang_watch.cpp"
    # Frame cap + opt-in performance log for the POB window; the pure parts are
    # also linked into the exe for --perf-selftest.
    "host/pob_frame_cap.cpp"
    "host/perf_log.cpp"
    "engine/render/r_perf_gpu.cpp"
    "ui_console.cpp"
    "ui_console.h"
    "ui_debug.cpp"
    "ui_debug.h"
    "ui_local.h"
    "ui_main.cpp"
    "ui_main.h"
    "ui_subscript.cpp"
    "ui_subscript.h"
    "translate/translation_manager.cpp"
    "translate/translation_manager.h"
    "translate/game_rules.h"
    "translate/rules_poe1.cpp"
    "translate/rules_poe2.cpp"
    "translate/startup_trace.cpp"
    "translate/startup_trace.h"
)

set (SIMPLEGRAPHIC_PLATFORM_SOURCES)
if (APPLE)
    set (SIMPLEGRAPHIC_PLATFORM_SOURCES
        "engine/system/win/sys_macos.mm"
    )
endif()

# APPEND, not set: a plain set() here would discard the APPLE block above,
# which is why sys_macos.mm was never actually compiled.
if (WIN32)
    list (APPEND SIMPLEGRAPHIC_PLATFORM_SOURCES
        "engine/system/win/sys_console.cpp"
        "engine/headless_ipc.cpp"
        "SimpleGraphic.rc"
    )
else()
    list (APPEND SIMPLEGRAPHIC_PLATFORM_SOURCES
        "engine/system/win/sys_console_unix.cpp"
        "engine/headless_ipc_posix.cpp"
    )
endif()

add_library(SimpleGraphic SHARED
    ${SIMPLEGRAPHIC_SOURCES}
    ${SIMPLEGRAPHIC_PLATFORM_SOURCES}
)
target_compile_definitions(SimpleGraphic
    PRIVATE
    "UNICODE"
    "_CRT_SECURE_NO_DEPRECATE"
    "_CRT_SECURE_NO_WARNINGS"
    "_SCL_SECURE_NO_DEPRECATE"
    "_SCL_SECURE_NO_WARNINGS"
    "GLFW_INCLUDE_NONE"
    "GL_SILENCE_DEPRECATION"
    "SIMPLEGRAPHIC_EXPORTS"
)

target_include_directories(SimpleGraphic
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine"
)

find_package(unofficial-angle CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(gli CONFIG REQUIRED)
find_package(LuaJIT REQUIRED)
find_package(Microsoft.GSL CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Freetype REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(sol2 CONFIG REQUIRED)
find_package(Threads REQUIRED)
find_package(zstd REQUIRED)
find_package(ZLIB REQUIRED)
find_package(WebP)

add_library(cmp_core STATIC
    dep/compressonator/cmp_core/source/cmp_core.cpp
    dep/compressonator/cmp_core/source/cmp_core.h
    dep/compressonator/cmp_core/shaders/bc3_encode_kernel.cpp
    dep/compressonator/cmp_core/shaders/bc7_encode_kernel.cpp
)

target_include_directories(cmp_core PRIVATE
    dep/compressonator/applications/_libs/cmp_math
)

target_include_directories(cmp_core PUBLIC
    dep/compressonator/cmp_core/shaders
    dep/compressonator/cmp_core/source
)

# Shared between the SimpleGraphic target (imgui static lib, /MD) and the
# pob-zh host exe, which compiles these sources directly so they pick up the
# exe's static CRT (/MT) — see the pob-zh target below.
set(IMGUI_SOURCES
    dep/imgui/imconfig.h
    dep/imgui/imgui.cpp
    dep/imgui/imgui.h
    dep/imgui/imgui_demo.cpp
    dep/imgui/imgui_draw.cpp
    dep/imgui/imgui_internal.h
    dep/imgui/imgui_tables.cpp
    dep/imgui/imgui_widgets.cpp
    dep/imgui/imstb_rectpack.h
    dep/imgui/imstb_textedit.h
    dep/imgui/imstb_truetype.h
    dep/imgui/backends/imgui_impl_glfw.cpp
    dep/imgui/backends/imgui_impl_glfw.h
    dep/imgui/backends/imgui_impl_opengl3.cpp
    dep/imgui/backends/imgui_impl_opengl3.h
    dep/imgui/backends/imgui_impl_opengl3_loader.h
    dep/imgui/misc/cpp/imgui_stdlib.cpp
    dep/imgui/misc/cpp/imgui_stdlib.h
)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_compile_definitions(imgui PUBLIC
    "IMGUI_IMPL_OPENGL_ES2"
)

target_include_directories(imgui PUBLIC
    dep/imgui
    dep/imgui/backends
    dep/imgui/misc/cpp
)

target_link_libraries(imgui PUBLIC
    unofficial::angle::libGLESv2
    glfw
)

target_include_directories(SimpleGraphic
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dep/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dep/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann
    ${CMAKE_CURRENT_SOURCE_DIR}/translate
)

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_link_options(SimpleGraphic
        PRIVATE
        "-export-dynamic"
    )
endif ()

if (WIN32)
    target_compile_options(SimpleGraphic
        PRIVATE
        "/EHa"
        # Same reason as the pob-zh target below: ui_api.cpp now carries UTF-8
        # CJK literals (what each POB patch is FOR, so the failure log can say it
        # in the user's words). Without this MSVC reads the file as Big5, where a
        # trailing 0x5C escapes the closing quote -- the error it produces is
        # "newline in constant", which points nowhere near the real problem.
        # Verified safe: before this change the engine had ZERO non-ASCII string
        # literals; every other non-ASCII byte in these sources is in a comment.
        "/utf-8"
    )
    target_compile_definitions(SimpleGraphic
        PRIVATE
        "_CRT_SECURE_NO_DEPRECATE=1"
        "_CRT_SECURE_NO_WARNINGS=1"
        "_SCL_SECURE_NO_DEPRECATE=1"
        "_SCL_SECURE_NO_WARNINGS=1"
    )
    target_link_libraries(SimpleGraphic
        PRIVATE
        "winmm.lib"
    )
endif ()

if (APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
    find_library(APPLICATION_SERVICES_LIBRARY ApplicationServices)
    target_link_libraries(SimpleGraphic
        PRIVATE
        ${CORE_FOUNDATION_LIBRARY}
        ${APPLICATION_SERVICES_LIBRARY}
        # libEGL: the engine asks EGL directly how big its drawing surface
        # really is, which is what the viewport has to match.
        unofficial::angle::libEGL
    )
endif ()

# zstd: Windows builds link the shared target; elsewhere vcpkg builds zstd
# static only, where the generic interface target is the portable choice.
if (WIN32)
    set(ZSTD_LINK_TARGET zstd::libzstd_shared)
else ()
    set(ZSTD_LINK_TARGET zstd::libzstd)
endif ()

target_link_libraries(SimpleGraphic
    PRIVATE
    unofficial::angle::libEGL
    unofficial::angle::libGLESv2
    cmp_core
    fmt::fmt
    Freetype::Freetype
    glfw
    gli
    glm::glm
    imgui
    LuaJIT::LuaJIT
    Microsoft.GSL::GSL
    re2::re2
    sol2
    Threads::Threads
    WebP::webpdecoder
    ZLIB::ZLIB
    ${ZSTD_LINK_TARGET}
)

# TARGET_RUNTIME_DLLS is empty on non-Windows; install(FILES "") errors.
if (WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:SimpleGraphic> DESTINATION "engine")
endif ()
# RUNTIME for Windows DLLs, LIBRARY for the dylibs this platform builds:
# both land where the loader looks, beside the executable tree.
install(TARGETS SimpleGraphic
        RUNTIME DESTINATION "engine"
        LIBRARY DESTINATION "engine")

# PoeCharm: engine-owned runtime CJK patch script, loaded by the engine into
# POB's Lua state after OnInit. Ships in engine/ (never the POB folder).
# The entry script loads exactly one of the per-game rule files beside it
# (使用者裁定 2026-10-30: PoE1 / PoE2 run their own rules).
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/poecharm_inject.lua"
              "${CMAKE_CURRENT_SOURCE_DIR}/inject_poe1.lua"
              "${CMAKE_CURRENT_SOURCE_DIR}/inject_poe2.lua" DESTINATION "engine")

# Headless bridge: the Lua side of the new UI, loaded after the inject script
# when POB_ZH_HEADLESS=1. Under Data\ (not engine\) so the signed translation-
# data line can hotfix it between app releases.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/bridge/bridge.lua"
              "${CMAKE_CURRENT_SOURCE_DIR}/host/data/bridge/mcp_tools.lua"
              "${CMAKE_CURRENT_SOURCE_DIR}/host/data/bridge/mcp_tools.json"
              "${CMAKE_CURRENT_SOURCE_DIR}/host/data/bridge/mcp_guide.md"
              "${CMAKE_CURRENT_SOURCE_DIR}/host/data/bridge/mcp_check_rules.json" DESTINATION "Data/bridge")

# Fallback pure-Lua runtime modules. POB's built-in updater does not ship new
# runtime lua files (e.g. sha2.lua, required by PoEAPI since POB 2.66), so a
# user who updated POB in place lacks them. package.path prefers the POB
# folder's own copy (engine/lua is appended last in ui_api.cpp InitAPI).
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/runtime_lua/sha2.lua" DESTINATION "engine/lua")

# Microsoft's redistributable HLSL compiler, vendored in host/data/redist/.
# ANGLE loads d3dcompiler_47.dll by name to compile the HLSL it generates. POB
# ships its own copy next to its exe, and the exe directory is first in the DLL
# search order -- which is why the stock POB works under Wine. Our engine lives
# in engine\ with SetDllDirectoryW pointing there; without a copy here the load
# falls through to the system one. On Windows that is Microsoft's too and
# nothing is amiss, but Wine's is a reimplementation that rejects ANGLE's output
# (E5022: resource index "must be determinable at compile time"), so POB will
# not start at all on macOS/CrossOver.
#
# The vendored file is POB's own copy (FileVersion 10.0.10150.0, the one
# field-tested under CrossOver 26.3 on 2026-09-01), so packaging never depends
# on a POB or Windows SDK install being present on the build machine. Like
# host/data/fonts/, the copy in dist\engine\ is mirrored by a sha256 assertion
# in tools/package_release.ps1.
if (WIN32)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/redist/d3dcompiler_47.dll" DESTINATION "engine")
endif ()

if (WIN32)
    set(DEPS_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

    if (NOT VCPKG_TARGET_TRIPLET MATCHES "-release")
        find_file(LuaJIT_DLL_DEBUG NAMES "lua51.dll"
            PATHS "${DEPS_DIR}" PATH_SUFFIXES "debug/bin" REQUIRED NO_DEFAULT_PATH)
        find_file(ZLIB_DLL_DEBUG NAMES "zlibd1.dll"
            PATHS "${DEPS_DIR}" PATH_SUFFIXES "debug/bin" REQUIRED NO_DEFAULT_PATH)

        install(FILES
            ${LuaJIT_DLL_DEBUG}
            ${ZLIB_DLL_DEBUG}
            DESTINATION "engine"
            CONFIGURATIONS Debug
        )
    endif ()

    find_file(LuaJIT_DLL_RELEASE NAMES "lua51.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)
    find_file(ZLIB_DLL_RELEASE NAMES "zlib1.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)

    # freetype's private vcpkg deps; $<TARGET_RUNTIME_DLLS> cannot see them,
    # so install them explicitly or SimpleGraphic.dll fails to load at runtime.
    find_file(BZ2_DLL_RELEASE NAMES "bz2.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)
    find_file(PNG_DLL_RELEASE NAMES "libpng16.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)
    find_file(BROTLIDEC_DLL_RELEASE NAMES "brotlidec.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)
    find_file(BROTLICOMMON_DLL_RELEASE NAMES "brotlicommon.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)
    # WebView2 SDK loader for the new UI window (host --modern-ui). Delay-loaded
    # by pob-zh.exe like glfw3.dll, so it resolves from engine\ after
    # SetDllDirectoryW and a machine without it fails with a message, not a
    # missing-DLL box before main. The Evergreen Runtime itself is not shipped.
    find_file(WEBVIEW2_LOADER_DLL_RELEASE NAMES "WebView2Loader.dll"
        PATHS "${DEPS_DIR}" PATH_SUFFIXES "bin" REQUIRED NO_DEFAULT_PATH)

    install(FILES
        ${LuaJIT_DLL_RELEASE}
        ${ZLIB_DLL_RELEASE}
        ${BZ2_DLL_RELEASE}
        ${PNG_DLL_RELEASE}
        ${BROTLIDEC_DLL_RELEASE}
        ${BROTLICOMMON_DLL_RELEASE}
        ${WEBVIEW2_LOADER_DLL_RELEASE}
        DESTINATION "engine"
        CONFIGURATIONS Release MinSizeRel RelWithDebInfo
    )
endif ()


# pob-zh host exe: ImGui launcher UI + thin loader that loads SimpleGraphic.dll
# and runs an external POB Launch.lua in a child process.
#
# The exe lives in the dist ROOT while every DLL lives in engine\, so:
#   - static CRT (/MT): no msvcp140/vcruntime load-time imports
#   - imgui sources are compiled into the exe (the imgui static lib is /MD)
#   - glfw3/libGLESv2 are DELAYLOADed; wWinMain calls SetDllDirectoryW(engine)
#     before the first glfw/GL call so they resolve from engine\
if (WIN32 OR APPLE)
    set(POBZH_SOURCES
        host/host_main.cpp
        host/error_log.cpp
        host/error_log_selftest.cpp
        host/hang_watch.cpp
        host/hang_watch_selftest.cpp
        # idle / minimised / unchanged-frame pacing of the ImGui loops
        host/frame_pacing.cpp
        host/frame_pacing_selftest.cpp
        host/live_resize.cpp
        # POB window frame cap + performance log (engine code, tested headless here)
        host/pob_frame_cap.cpp
        host/perf_log.cpp
        host/perf_selftest.cpp
        host/launcher_config.cpp
        host/launcher_strings_io.cpp
        host/ui_theme.cpp
        # launcher widgets from the design system (cards, rows, switches, toasts)
        host/ui_icons_data.cpp
        host/gl_shot.cpp
        host/ui_widgets.cpp
        host/launcher_ui.cpp
        host/launcher_editor.cpp
        host/editor_data.cpp
        host/editor_selftest.cpp
        host/trans_editor_logic.cpp
        host/trans_editor_selftest.cpp
        host/paste_selftest.cpp
        # POB 執行時拼出來的物品名(「<標題>, <基底>」)的正向翻譯回歸
        host/item_name_selftest.cpp
        host/paste_trace.cpp
        # 多佔位符詞條的填位回歸 + 全量 A/B 快照工具
        host/placeholder_selftest.cpp
        # stats_by_id.json 單行 stat 查詢的契約(translation_lookup_stat)
        host/stat_selftest.cpp
        host/pob_launch.cpp
        host/bridge_gate.cpp
        # pob:// / pob2:// "Open in PoB" links (HKCU registration + URI whitelist)
        host/pob_protocol.cpp
        host/window_manager.cpp
        # window_dock: the Windows version adopts POB windows into the launcher,
        # which needs Win32 window ownership; the POSIX counterpart keeps the
        # tab list without adopting.
        $<$<BOOL:${WIN32}>:host/window_dock.cpp>
        $<$<NOT:$<BOOL:${WIN32}>>:host/window_dock_posix.cpp>
        # the editor selftest asks the REAL loader what the engine would
        # return; without this it could only re-implement the merge and
        # would be testing its own copy of the rule instead of the engine
        translate/translation_manager.cpp
        translate/rules_poe1.cpp
        translate/rules_poe2.cpp
        translate/startup_trace.cpp
        host/filter_parser.cpp
        host/filter_doc_editor.cpp
        host/filter_undo.cpp
        host/filter_edit_journal.cpp
        host/section_journal.cpp
        host/filter_file_watch.cpp
        host/filter_selftest.cpp
        host/filter_schema.cpp
        host/filter_card_ui.cpp
        host/filter_batch.cpp
        host/custom_rules_io.cpp
        host/sound_library_service.cpp
        host/section_filteredit.cpp
        host/section_sounds.cpp
        host/filter_preview.cpp
        host/filter_item_import.cpp
        host/filter_data.cpp
        host/filter_i18n.cpp
        host/image_tex.cpp
        # dropped from the build when the old filter tier-list view was retired;
        # back in for the atlas planner's scarab icons
        host/icon_manager.cpp
        host/item_library.cpp
        host/audio_player.cpp
        host/sound_manager.cpp
        host/clipboard_util.cpp
        host/editor_util.cpp
        host/editor_shell.cpp
        host/filter_editor.cpp
        # one panel's content, drawn either into its own window (tool_window) or
        # into a launcher tab -- the per-frame code exists once for both
        host/tool_window.cpp
        # drives a panel exactly as a launcher tab does; no other selftest ever
        # creates an ImGui frame, so nothing else can see that path
        host/panel_selftest.cpp
        host/atlas_tree_data.cpp
        host/atlas_view.cpp
        host/atlas_planner.cpp
        host/atlas_import.cpp
        host/atlas_update.cpp
        host/data_updater.cpp
        host/data_update_selftest.cpp
        host/atlas_stat_agg.cpp
        host/atlas_scarabs.cpp
        host/atlas_threads.cpp
        host/atlas_threads_selftest.cpp
        host/atlas_astrolabes.cpp
        host/atlas_maps.cpp
        # league-mechanic grouping behind "show me every 裂痕 cluster"; the map
        # itself is generated per season by the updater/importer, not shipped as
        # a hand-maintained data file
        host/atlas_mechanics.cpp
        # shared by the scarab / astrolabe / map pickers
        host/fuzzy_match.cpp
        host/atlas_optimize.cpp
        host/atlas_persist.cpp
        host/atlas_i18n.cpp
        host/atlas_version_index.cpp
        host/atlas_diff.cpp
        host/timeless_jewel.cpp
        # Abyss jewels are keyed by socket, not by radius; separate engine
        host/timeless_jewel_abyss.cpp
        host/timeless_jewel_ui.cpp
        host/passive_tree_data.cpp
        host/passive_tree_view.cpp
        host/passive_import.cpp
        host/passive_tree_update.cpp
        host/app_update.cpp
        # headless POB child (stdio JSON) + its self-test against a sandbox install
        host/headless_proc.cpp
        host/headless_selftest.cpp
        host/mcp_selftest.cpp
        host/mcp_server.cpp
        host/mcp_config.cpp
        host/pob_sandbox.cpp
        # the new UI: a Win32 window hosting WebView2, forwarding lines between
        # the Svelte page (dist\ui) and the headless POB child
        # ...or, where WebView2 cannot run (Wine / CrossOver), the same page in
        # the system browser over a loopback HTTP server
        host/modern_ui_browser.cpp
        host/modern_ui_shared.cpp
        host/zip_extract.cpp
        host/pob_migrate.cpp
        host/pob_migrate_selftest.cpp
        host/hash_sha256.cpp
        host/sig_verify.cpp
        # 倉庫收益統計（測試性質，POESESSID 通道）：provider + 資料層 + 服務 + UI + 自檢
        host/warehouse_provider_sessid.cpp
        host/warehouse_snapshot.cpp
        host/warehouse_state.cpp
        host/warehouse_pricing.cpp
        host/warehouse_ninja.cpp
        host/warehouse_service.cpp
        host/warehouse_ui.cpp
        host/warehouse_selftest.cpp
        # 設定頁可停用的字典檔(POB_ZH_DICT_SWITCHES)從 ini 到引擎載入
        host/dict_switch_selftest.cpp
        # 輿圖策略 × 倉庫收益：每張圖成本（純資料）+ 共用 poe.ninja 價格來源
        host/atlas_cost.cpp
        host/warehouse_price_feed.cpp
        # 王戰收益:掉率表 x poe.ninja 價格(純資料 + 面板 + 自檢)
        host/boss_profit.cpp
        host/boss_profit_ui.cpp
        host/boss_profit_selftest.cpp
        dep/miniz/miniz.c
        engine/common/base64.c
        host/http_client.cpp
        host/github_token.cpp
        ${IMGUI_SOURCES}
    )

    # The WIN32 flag selects the GUI subsystem, which only Windows has; the
    # resource script and the WebView2 window likewise exist only there.
    if (WIN32)
        list(APPEND POBZH_SOURCES
            host/pob-zh.rc
            host/modern_ui_window.cpp
        )
        add_executable(pob-zh WIN32 ${POBZH_SOURCES})
    else ()
        # POSIX entry point, and the modern UI bridged to the browser path.
        list(APPEND POBZH_SOURCES
            host/host_main_posix.cpp
            host/modern_ui_posix.cpp
        )
        add_executable(pob-zh ${POBZH_SOURCES})
    endif ()

    target_compile_definitions(pob-zh PRIVATE "UNICODE" "_UNICODE" "IMGUI_IMPL_OPENGL_ES2")
    # 更新來源的 repo。不設就是 app_update.cpp 裡的正式值。
    #
    # 存在的理由:「簽章會放行正確的更新」只能對著一個真的 release 證明,而拿正式
    # repo 做那個實驗會讓全體使用者的客戶端看到測試用的 release —— 尤其 Data 線掃
    # /releases 全清單、prerelease 不會被跳過。
    #
    #   cmake -B build -DPOBTOOLS_UPDATE_REPO=你的帳號/測試repo
    #   cmake -B build -UPOBTOOLS_UPDATE_REPO      # 改回正式值
    #
    # ⚠ 這是編譯期的,執行期改不了 —— 更新來源是安全邊界的一部分。
    # ⚠ 打包腳本 Assert-Step 16 會問 exe 自己(--update-source)並斷言是正式 repo,
    #    所以測試建置不可能被打包發出去。
    if(POBTOOLS_UPDATE_REPO)
        message(STATUS "pob-zh: 更新來源改為 ${POBTOOLS_UPDATE_REPO}(測試建置,不可發布)")
        target_compile_definitions(pob-zh PRIVATE "POBTOOLS_UPDATE_REPO=\"${POBTOOLS_UPDATE_REPO}\"")
    endif()
    # beta 建置的版號後綴(beta-<commit 短雜湊 7 碼>),不設就是正式版號。
    #   cmake -B build -DPOBTOOLS_VERSION_SUFFIX=beta-1ef80a6
    #   cmake -B build -UPOBTOOLS_VERSION_SUFFIX   # 發完立刻改回
    # ⚠ cache 會留著:打包腳本 -Mode App 遇到帶後綴的 exe 會擋。
    if(POBTOOLS_VERSION_SUFFIX)
        if(NOT POBTOOLS_VERSION_SUFFIX MATCHES "^beta-[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]$")
            message(FATAL_ERROR "POBTOOLS_VERSION_SUFFIX 必須是 beta-<7 碼小寫 hex>,收到 ${POBTOOLS_VERSION_SUFFIX}")
        endif()
        message(STATUS "pob-zh: beta 建置,版號後綴 ${POBTOOLS_VERSION_SUFFIX}")
        target_compile_definitions(pob-zh PRIVATE "POBTOOLS_VERSION_SUFFIX=\"${POBTOOLS_VERSION_SUFFIX}\"")
    endif()
    # host sources contain UTF-8 CJK string literals; force UTF-8 source parsing
    # so MSVC does not misread them under the system (Big5) code page.
    # MSVC would otherwise read these UTF-8 CJK literals under the system
    # (Big5) code page. clang reads UTF-8 by default and rejects the flag.
    if (MSVC)
        target_compile_options(pob-zh PRIVATE "/utf-8")
    endif ()
    target_include_directories(pob-zh PRIVATE
        dep/imgui
        dep/imgui/backends
        dep/imgui/misc/cpp
        dep/stb
        dep/miniz
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/nlohmann
    )
    if (MSVC)
        set_property(TARGET pob-zh PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif ()
    # webpdecoder: the 3.30+ atlas sprite sheets are webp. Its DLL already ships
    # in engine/ for SimpleGraphic, hence delay-loaded like glfw3/libGLESv2.
    if (WIN32)
        find_package(unofficial-webview2 CONFIG REQUIRED)
        target_link_libraries(pob-zh PRIVATE glfw unofficial::angle::libGLESv2 unofficial::webview2::webview2 WebP::webpdecoder delayimp)
        target_link_options(pob-zh PRIVATE "/DELAYLOAD:glfw3.dll" "/DELAYLOAD:libGLESv2.dll" "/DELAYLOAD:WebView2Loader.dll" "/DELAYLOAD:libwebpdecoder.dll")
    else ()
        # No WebView2 and no delay loading: the modern UI falls back to the
        # system browser here, which modern_ui_browser.cpp already implements.
        # CURL: the WinHTTP shim (engine/common/win_compat_http.h) is built on
        # it, so the launcher needs it directly here.
        find_package(CURL REQUIRED)
        # libEGL as well as libGLESv2: the launcher asks EGL directly for the
        # real size of the drawing surface, which is what the viewport has to
        # match (see host/launcher_ui.cpp).
        if (APPLE)
            # The launcher uses the ENGINE's window library, not a copy of its own.
            #
            # Linked statically into both, there are two of them in one process:
            # two sets of window-system state, both claiming the one macOS
            # application object and the one event loop. That is what stops POB
            # from running inside the launcher -- and running it outside is why
            # each build is its own program, and why the Dock cannot gather them
            # under one icon.
            #
            # SimpleGraphic exports the window functions (T _glfwInit), so the
            # launcher resolves them there and the two share one library.
            target_link_libraries(pob-zh PRIVATE SimpleGraphic unofficial::angle::libEGL
                                  unofficial::angle::libGLESv2 WebP::webpdecoder CURL::libcurl)
        else ()
            target_link_libraries(pob-zh PRIVATE glfw unofficial::angle::libEGL
                                  unofficial::angle::libGLESv2 WebP::webpdecoder CURL::libcurl)
        endif ()
        if (APPLE)
            # Security: the DPAPI shim (engine/common/win_compat_crypt.h) keeps
            # the two credentials in the login keychain, which is the local
            # equivalent of DPAPI's machine-and-user binding.
            find_library(SECURITY_LIBRARY Security)
            target_link_libraries(pob-zh PRIVATE ${SECURITY_LIBRARY})
        endif ()
    endif ()
    set_target_properties(pob-zh PROPERTIES OUTPUT_NAME "pob-zh")
    install(TARGETS pob-zh RUNTIME DESTINATION ".")
    if (APPLE)
        # ANGLE is the one dependency built as a shared library (see
        # vcpkg-triplets/arm64-osx-pobtools.cmake for why), so it has to travel
        # with the executable. On Windows $<TARGET_RUNTIME_DLLS> does this; that
        # generator expression is empty here, so the files are named directly.
        #
        # They sit beside the executable because that is what the rpath says,
        # and because GLFW's EGL backend loads them by name at runtime.
        file(GLOB POBTOOLS_ANGLE_LIBS
             "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/*EGL*.dylib"
             "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/*GLESv2*.dylib")
        if (POBTOOLS_ANGLE_LIBS)
            install(FILES ${POBTOOLS_ANGLE_LIBS} DESTINATION ".")
            # Two names are needed for the same files. The executable links
            # against them under vcpkg's names (liblibEGL_angle.dylib), while
            # GLFW's EGL backend loads them at runtime by the platform's
            # standard names -- src/egl_context.c asks dlopen for exactly
            # "libEGL.dylib" and "libGLESv2.dylib". Symlinks satisfy both
            # without shipping the libraries twice.
            install(CODE "
                foreach(pair \"libEGL.dylib;EGL\" \"libGLESv2.dylib;GLESv2\")
                    list(GET pair 0 link_name)
                    list(GET pair 1 token)
                    file(GLOB real \"\${CMAKE_INSTALL_PREFIX}/*\${token}*.dylib\")
                    list(FILTER real EXCLUDE REGEX \"/\${link_name}$\")
                    if(real)
                        list(GET real 0 target)
                        get_filename_component(target_name \"\${target}\" NAME)
                        execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink
                                        \"\${target_name}\" \"\${CMAKE_INSTALL_PREFIX}/\${link_name}\")
                        message(STATUS \"ANGLE: \${link_name} -> \${target_name}\")
                    endif()
                endforeach()
            ")
        else ()
            message(WARNING
                "pob-zh: no ANGLE dylib found under ${VCPKG_TARGET_TRIPLET}. "
                "Configure with -DVCPKG_TARGET_TRIPLET=arm64-osx-pobtools "
                "-DVCPKG_HOST_TRIPLET=arm64-osx-pobtools and "
                "-DVCPKG_OVERLAY_TRIPLETS=<repo>/pob-zh-engine/vcpkg-triplets, "
                "or the launcher will fail to create its window.")
        endif ()
    endif ()

    if (APPLE AND NOT POBTOOLS_NO_BUNDLE)
        # Contents/Info.plist, one level up from Contents/MacOS.
        set(POBTOOLS_VERSION "${PROJECT_VERSION}")
        if (NOT POBTOOLS_VERSION)
            set(POBTOOLS_VERSION "1.0")
        endif ()
        configure_file("${CMAKE_CURRENT_SOURCE_DIR}/host/data/Info.plist.in"
                       "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" DESTINATION "..")

        # The program's own icon, in the form the Finder reads.
        #
        # host/pobtools.ico is what Windows uses, and holds a 256 px image.
        # macOS wants an .icns, which is a set of sizes rather than one image,
        # so the sizes are rendered from it here. Both tools ship with the
        # system. Above 256 the image is enlarged rather than redrawn -- those
        # entries exist so the Finder has something to show at its largest
        # sizes instead of nothing.
        set(POBTOOLS_ICON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/host/pobtools.ico")
        set(POBTOOLS_ICONSET "${CMAKE_CURRENT_BINARY_DIR}/PobTools.iconset")
        set(POBTOOLS_ICNS "${CMAKE_CURRENT_BINARY_DIR}/PobTools.icns")
        add_custom_command(
            OUTPUT "${POBTOOLS_ICNS}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf "${POBTOOLS_ICONSET}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${POBTOOLS_ICONSET}"
            # .ico first to a plain image: sips will not enlarge straight from it.
            COMMAND sips -s format png "${POBTOOLS_ICON_SRC}"
                    --out "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png" > /dev/null
            COMMAND sips -z 16 16 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_16x16.png" > /dev/null
            COMMAND sips -z 32 32 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_16x16@2x.png" > /dev/null
            COMMAND sips -z 32 32 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_32x32.png" > /dev/null
            COMMAND sips -z 64 64 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_32x32@2x.png" > /dev/null
            COMMAND sips -z 128 128 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_128x128.png" > /dev/null
            COMMAND sips -z 256 256 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_128x128@2x.png" > /dev/null
            COMMAND sips -z 256 256 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_256x256.png" > /dev/null
            COMMAND sips -z 512 512 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_256x256@2x.png" > /dev/null
            COMMAND sips -z 512 512 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_512x512.png" > /dev/null
            COMMAND sips -z 1024 1024 "${CMAKE_CURRENT_BINARY_DIR}/pobtools_icon.png"
                    --out "${POBTOOLS_ICONSET}/icon_512x512@2x.png" > /dev/null
            COMMAND iconutil -c icns "${POBTOOLS_ICONSET}" -o "${POBTOOLS_ICNS}"
            DEPENDS "${POBTOOLS_ICON_SRC}"
            COMMENT "Rendering the app icon for the Finder"
            VERBATIM)
        add_custom_target(pobtools_icon ALL DEPENDS "${POBTOOLS_ICNS}")
        install(FILES "${POBTOOLS_ICNS}" DESTINATION "../Resources")
        # Most dylibs sit beside the executable; SimpleGraphic, which the
        # launcher now shares a window library with, sits in engine/.
        set_target_properties(pob-zh PROPERTIES
            INSTALL_RPATH "@executable_path;@executable_path/engine"
            BUILD_WITH_INSTALL_RPATH FALSE)
    endif ()
    # Bundled item-name -> icon art-path map (generated from RePoE).
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/icon_paths.json" DESTINATION "Data")
    # Complete Traditional-Chinese item dataset (repoe-fork: names + class + tags).
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/filter_items_zh.json" DESTINATION "Data")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/item_meta.json" DESTINATION "Data")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/item_classes_zh.json" DESTINATION "Data")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/item_classes_en.json" DESTINATION "Data")
    # Bundled starter filter (NeverSink POE1) for the "load default template" preset.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/default.filter" DESTINATION "Filters")
    # PoE1 atlas passive tree (generated by tools/gen_atlas_tree.py) + sprite sheets.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/timeless_jewels.json" DESTINATION "Data")
    # PoE1 character passive tree geometry for the Timeless Jewel tree view
    # (tools/gen_passive_tree.py). Sprite SHEETS are read from the user's POB
    # TreeData folder at runtime, never bundled.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/passive_tree_poe1.json" DESTINATION "Data")
    # Bundled passive-tree sprite sheets (zoom -3, from GGG skilltree-export):
    # lets the jewel tree view draw a league PoB hasn't shipped yet; the view
    # still falls back to the user's PoB TreeData/<ver>/ when a sheet is absent.
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/host/data/tree/" DESTINATION "Data/tree")
    # PoE1 atlas passive tree — versioned layout: two seasons live side by side
    # under Data/atlas_versions/<tag>/ (condensed tree + EN/TC node mapping +
    # sprite sheets, each self-consistent) so the planner can diff them.
    # atlas_index.json registers the installed seasons and the active/compare
    # selection; the in-app updater installs new seasons and prunes to two.
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/host/data/atlas_versions/" DESTINATION "Data/atlas_versions")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/atlas_index.json" DESTINATION "Data")
    # Scarab catalogue for the planner's map-device section: names + official
    # effect text in both locales, art paths, and the placement rules
    # (per-scarab copy limit, mutual-exclusion family). Regenerated from a local
    # Content.ggpk by tools/ggpk_zh/gen_scarabs.py --verify; unlike the atlas
    # tree it has no online updater, so a new league needs a fresh run.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/scarabs_poe1.json" DESTINATION "Data")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/boss_drops_poe1.json" DESTINATION "Data")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/boss_items_zh.json" DESTINATION "Data")
    # Astrolabe catalogue (3.29 Shaped Regions) and the atlas map list behind the
    # planner's quadrant slots and main-map pick. Regenerated from a local
    # Content.ggpk by tools/ggpk_zh/gen_astrolabes.py --verify and
    # gen_atlas_maps.py --verify. Like the scarabs these have NO online updater,
    # so a new league needs a fresh run of both.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/astrolabes_poe1.json" DESTINATION "Data")
    # 3.30 thread catalogue (names from GGPK BaseItemTypes); no online updater either.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/threads_poe1.json" DESTINATION "Data")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/atlas_maps_poe1.json" DESTINATION "Data")
    # The shipped CJK font. Until 2026-08 this file existed ONLY in dist\Fonts\,
    # which is gitignored and has no install rule -- so a `cmake --install` into a
    # fresh prefix, or any cleanup of dist\, silently lost it. Nothing reported the
    # loss either: the launcher falls through ResolveFontPath to the next candidate
    # and the engine to its CJK fallback, so the first symptom is missing glyphs on
    # someone else's machine. host/data/fonts/ is now the single source, mirrored by
    # the sha256 assertion in tools/package_release.ps1.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/fonts/NotoSansTC-Regular.ttf" DESTINATION "Fonts")
    # Korean (ko-KR): Noto Sans TC carries no Hangul at all (0 of 11,172
    # syllables), so the Korean dictionaries need their own face. Static Regular
    # instance of Google's NotoSansKR[wght].ttf (SIL OFL 1.1, see NOTICE.md); the
    # engine and the launcher both pick it up as a glyph fallback, the default
    # face stays Noto Sans TC.
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/host/data/fonts/NotoSansKR-Regular.ttf" DESTINATION "Fonts")
endif ()


# lcurl module

set(LCURL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/Lua-cURLv3)
file(GLOB LCURL_SOURCES ${LCURL_SOURCE_DIR}/src/**.c)
add_library(lcurl SHARED ${LCURL_SOURCES})

target_include_directories(lcurl
    PRIVATE
    ${LCURL_SOURCE_DIR}/src
)

target_link_libraries(lcurl
    PRIVATE
    CURL::libcurl
    LuaJIT::LuaJIT
)

# RUNTIME for Windows DLLs, LIBRARY for the dylibs this platform builds:
# both land where the loader looks, beside the executable tree.
install(TARGETS lcurl
        RUNTIME DESTINATION "engine"
        LIBRARY DESTINATION "engine")
# TARGET_RUNTIME_DLLS is empty on non-Windows; install(FILES "") errors.
if (WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:lcurl> DESTINATION "engine")
endif ()

# luautf8 module

add_library(lua-utf8 SHARED libs/luautf8/lutf8lib.c)

# LUA_BUILD_AS_DLL switches lua.h to __declspec export/import, which only
# exists on Windows; elsewhere the default visibility is already correct.
if (WIN32)
    target_compile_definitions(lua-utf8
        PRIVATE
        LUA_BUILD_AS_DLL
    )
endif ()

target_include_directories(lua-utf8
    PRIVATE
)

target_link_libraries(lua-utf8
    PRIVATE
    LuaJIT::LuaJIT
)

# RUNTIME for Windows DLLs, LIBRARY for the dylibs this platform builds:
# both land where the loader looks, beside the executable tree.
install(TARGETS lua-utf8
        RUNTIME DESTINATION "engine"
        LIBRARY DESTINATION "engine")
# TARGET_RUNTIME_DLLS is empty on non-Windows; install(FILES "") errors.
if (WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:lua-utf8> DESTINATION "engine")
endif ()

# luasocket module

add_library(luasocket SHARED
   "libs/luasocket/src/auxiliar.c"
   "libs/luasocket/src/buffer.c"
   "libs/luasocket/src/compat.c"
   "libs/luasocket/src/except.c"
   "libs/luasocket/src/inet.c"
   "libs/luasocket/src/io.c"
   "libs/luasocket/src/luasocket.c"
   "libs/luasocket/src/options.c"
   "libs/luasocket/src/select.c"
   "libs/luasocket/src/tcp.c"
   "libs/luasocket/src/timeout.c"
   "libs/luasocket/src/udp.c"
)

# luasocket ships both socket backends; pick the one for this platform.
if (WIN32)
    target_sources(luasocket PRIVATE "libs/luasocket/src/wsocket.c")
else ()
    target_sources(luasocket PRIVATE "libs/luasocket/src/usocket.c")
endif ()

target_include_directories(luasocket
    PRIVATE
    ${LSOCKET_SOURCE_DIR}/src
)

target_link_libraries(luasocket
    PRIVATE
    LuaJIT::LuaJIT
)
if (WIN32)
    target_link_libraries(luasocket PRIVATE wsock32 ws2_32)
endif ()

set_target_properties( luasocket PROPERTIES OUTPUT_NAME "socket" ) 
# RUNTIME for Windows DLLs, LIBRARY for the dylibs this platform builds:
# both land where the loader looks, beside the executable tree.
install(TARGETS luasocket
        RUNTIME DESTINATION "engine"
        LIBRARY DESTINATION "engine")
# TARGET_RUNTIME_DLLS is empty on non-Windows; install(FILES "") errors.
if (WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:luasocket> DESTINATION "engine")
endif ()

# lzip module

add_library(lzip SHARED libs/LZip/lzip.cpp)

target_include_directories(lzip
    PRIVATE
)

target_link_libraries(lzip
    PRIVATE
    LuaJIT::LuaJIT
    ZLIB::ZLIB
)

# RUNTIME for Windows DLLs, LIBRARY for the dylibs this platform builds:
# both land where the loader looks, beside the executable tree.
install(TARGETS lzip
        RUNTIME DESTINATION "engine"
        LIBRARY DESTINATION "engine")
# TARGET_RUNTIME_DLLS is empty on non-Windows; install(FILES "") errors.
if (WIN32)
    install(FILES $<TARGET_RUNTIME_DLLS:lzip> DESTINATION "engine")
endif ()

# Headless check of the engine's fallback font chain against the shipped TTFs
# (engine/render/r_font_fallback.h). Not installed, not built by default:
#   cmake --build build --config Release --target font_fallback_selftest
#   $env:PATH = "dist\engine;$env:PATH"; build\Release\font_fallback_selftest.exe dist\Fonts
add_executable(font_fallback_selftest EXCLUDE_FROM_ALL tests/font_fallback_selftest.cpp)
target_link_libraries(font_fallback_selftest PRIVATE Freetype::Freetype)
if (MSVC)
    target_compile_options(font_fallback_selftest PRIVATE "/utf-8")
endif ()

# Offline PAT persistence/request tests. Fake WinHTTP transport, real DPAPI;
# no installed POB or GUI runtime is needed to run this executable.
if (WIN32)
    add_executable(github_token_test EXCLUDE_FROM_ALL
        tests/github_token_test.cpp host/github_token.cpp host/http_client.cpp)
    target_compile_definitions(github_token_test PRIVATE _WINHTTP_INTERNAL_)
    target_compile_options(github_token_test PRIVATE "/utf-8")
    set_property(TARGET github_token_test PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()
