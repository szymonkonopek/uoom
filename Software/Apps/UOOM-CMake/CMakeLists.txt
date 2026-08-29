# UOOM -- DOOM for the UNA Watch
#
# Ordering follows the SDK's example apps and is not free: una-app.cmake sets
# CMAKE_TOOLCHAIN_FILE, so it must be included *before* project(). The build
# functions then read SERVICE_SOURCES / GUI_SOURCES / *_INCLUDE_DIRS out of
# this scope by name, and una_app_build_app() looks for the two ELF targets by
# the exact names ${APP_NAME}Service.elf and ${APP_NAME}GUI.elf.
#
# One deliberate departure from every example: GUI_SOURCES does NOT include
# UNA_SDK_SOURCES_GUI. See Software/Libs/Sources/UoomMain.cpp -- that group owns
# both `main` and the touchgfx_* entry points, and it does not compile without a
# TouchGFX Designer project.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# ----------------------------------------------------------------- app identity

set(APP_NAME      "UOOM")
set(APP_USER_NAME "UOOM")
set(APP_TYPE      "Utility")
set(APP_AUTOSTART Off)
set(DEV_ID        "UNA")
# Issued by the app store when the app page was created -- not generated with
# tools/gen_app_id.py, which is only for an app that does not have one yet. The
# store matches uploads to the page by this id, and the watch identifies the
# app by it, so it must not change once anything is published.
set(APP_ID        "AB96CBAA81A51C63")

# --------------------------------------------------------------------- paths

set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH      "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(DOOM_PATH      "${CMAKE_CURRENT_SOURCE_DIR}/../../../third_party/doomgeneric/doomgeneric")

# UOOM has no TouchGFX project, but una_app_build_app() only merges the GUI ELF
# into the .uapp when TOUCHGFX_PATH is defined -- it checks for the variable,
# not for a project. So this is a flag, and nothing includes a touchgfx.cmake
# from it.
set(TOUCHGFX_PATH  "${CMAKE_CURRENT_SOURCE_DIR}")

# Bisection build: platform only, DOOM not linked. See uoom_smoke.c.
option(UOOM_SMOKE "Build the platform bring-up only, without DOOM" OFF)

if(NOT UOOM_SMOKE AND NOT EXISTS "${DOOM_PATH}/d_main.c")
    message(FATAL_ERROR
        "doomgeneric is not vendored yet. Run:\n"
        "  tools/fetch-doomgeneric.sh")
endif()

# --------------------------------------------------------------------- memory
#
# These become linker --defsym RAM_LENGTH / STACK_SIZE, feeding the single
# MEMORY region in the SDK's Sections.ld. Two things about that invert the usual
# embedded advice:
#
# 1. RAM_LENGTH is a *link-time ceiling*, not a reservation. The .uapp header
#    records real section sizes and the loader allocates from those, so raising
#    this costs nothing at run time and nothing in the SDK caps it.
# 2. That region has no flash counterpart: .text and .rodata live in it too, so
#    DOOM's code and its const trig tables count against the number.
#
# The zone heap is deliberately not in here -- uoom_zone_base() takes it from
# malloc, which on this platform is the kernel's allocator, outside the region.

set(UNA_APP_SERVICE_RAM_LENGTH  "64K")
set(UNA_APP_SERVICE_STACK_SIZE  "4*1024")
# Must hold everything: .text, .rodata, .data, .bss (including DOOM's static
# arrays, the 240x240 panel buffer, the 320x200 screen buffer and now the zone)
# and .stack. The zone alone is UOOM_ZONE_BYTES.
#
# It is a link-time ceiling with no documented cap and no runtime cost, so the
# only real question is what the kernel's loader will hand out -- measure that
# with tools/probe-loader.sh before assuming.
set(UNA_APP_GUI_RAM_LENGTH      "1750K")
# DOOM recurses through the BSP tree and the drawers nest deep; 10K is not it.
set(UNA_APP_GUI_STACK_SIZE      "24*1024")

# ------------------------------------------------------------------- SDK glue
#
# Sets the toolchain, so it comes before project().
include($ENV{UNA_SDK}/cmake/una-app.cmake)

# Sets SCRIPTS_PATH, UNA_SDK_SOURCES_*, UNA_SDK_INCLUDE_DIRS_*.
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)

# Sets LIBS_INCLUDE_DIRS and UOOM's own source groups.
include(${LIBS_PATH}/libs.cmake)

project(${APP_NAME})

# The version, and with it the package's filename. Pinned here rather than left
# to the SDK, which derives it from an `apps-*` git tag and falls back to
# "0.0.0-dev" when there is none -- so every build shipped as UOOM_0.0.0-dev.uapp
# no matter what had changed. una_app_setup_version takes an already-defined
# BUILD_VERSION as-is, so -DBUILD_VERSION=... on the command line still wins,
# and tagging apps-x.y.z later only means deleting these two lines.
if(NOT DEFINED BUILD_VERSION)
    set(BUILD_VERSION "0.0.1")
endif()

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# The DOOM translation units, and the renderer subset broken out so it can be
# built at a different optimisation level.
if(UOOM_SMOKE)
    set(DOOM_SOURCES "")
    set(DOOM_RENDERER_SOURCES "")
    message(STATUS "UOOM_SMOKE: DOOM is not linked; platform bring-up only")
else()
    include(${CMAKE_CURRENT_SOURCE_DIR}/doom-sources.cmake)
endif()

# The one file worth taking from UNA_SDK_SOURCES_GUI: despite the name it
# includes no TouchGFX headers, and it carries the whole kernel-side GUI
# protocol -- frame tick, STOP/SUSPEND/RESUME, the button queue, framebuffer
# submit.
set(UOOM_SDK_CMDPROC
    $ENV{UNA_SDK}/Libs/Source/Port/TouchGFX/TouchGFXCommandProcessor.cpp
)

# --------------------------------------------------------------------- flags

# ---------------------------------------------------------------- tuning knobs
#
# Real cache variables rather than raw -D, so they reach the compiler as macros
# and so `cmake -LH` lists them.
set(UOOM_ZONE_KB    ""  CACHE STRING "DOOM zone heap size in KB")
set(UOOM_RENDER_MODE ""  CACHE STRING "0 = 320x200 scaled, 1 = native 240x240")
set(UOOM_SCALE_MODE  ""  CACHE STRING "0 fill, 1 letterbox, 2 inscribed circle")

# One source of truth for the zone size: the region the service reserves and
# the array the compiler emits must agree, or the link fails with an overflow
# that names neither.
if(UOOM_ZONE_KB)
    set(UOOM_ZONE_KB_EFFECTIVE ${UOOM_ZONE_KB})
else()
    # 640 runs. The loader enforces a per-image ceiling of (878, 1009] KB and
    # a combined one of [1147, 1262) KB; a 640KB zone was too big until the
    # renderer limits came down and took 24KB off the GUI, putting the pair at
    # 1232264 bytes rather than 1262136.
    set(UOOM_ZONE_KB_EFFECTIVE 640)
endif()
math(EXPR _una_zone_bytes "${UOOM_ZONE_KB_EFFECTIVE} * 1024")
# Which build is on the watch? Fifteen flashes in, that stopped being obvious:
# the package name never changes, so a stale Output/ and a fresh one look
# identical. The short commit -- with a "+" when the tree is dirty, which it
# usually is mid-work -- goes into the image, onto the boot report and into
# uoom.log, and tools/build-watch.sh prints the same string. Comparing them is
# then a glance rather than a guess.
execute_process(
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short=7 HEAD
    OUTPUT_VARIABLE  UOOM_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
execute_process(
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} status --porcelain
    OUTPUT_VARIABLE  UOOM_GIT_DIRTY
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT UOOM_GIT_HASH)
    set(UOOM_GIT_HASH "nogit")
elseif(UOOM_GIT_DIRTY)
    set(UOOM_GIT_HASH "${UOOM_GIT_HASH}+")
endif()
message(STATUS "UOOM build id: ${UOOM_GIT_HASH}")
add_compile_definitions(UOOM_BUILD_ID="${UOOM_GIT_HASH}")

add_compile_definitions(UOOM_ZONE_BYTES=${_una_zone_bytes})
if(NOT UOOM_RENDER_MODE STREQUAL "")
    add_compile_definitions(UOOM_RENDER_MODE=${UOOM_RENDER_MODE})
endif()
if(NOT UOOM_SCALE_MODE STREQUAL "")
    add_compile_definitions(UOOM_SCALE_MODE=${UOOM_SCALE_MODE})
endif()

# Ballast: static .bss added to find out how large an image the kernel's loader
# will accept. UOOM_BALLAST_KB weighs down the GUI process (smoke builds only);
# UOOM_SVC_BALLAST_KB weighs down the service, which answers the separate and
# more interesting question of whether the ceiling is per *process*.
# See docs/08-first-boot-debugging.md.
set(UOOM_BALLAST_KB     "0" CACHE STRING "KB of static ballast in the GUI (smoke only)")
set(UOOM_SVC_BALLAST_KB "0" CACHE STRING "KB of static ballast in the service")

# The zone in the service process. Default ON, because the single-process
# arrangement is not merely worse -- it is measured to be impossible. The
# loader hands out between 878KB and 1009KB per app image
# (tools/probe-loader.sh) and the GUI needs 599KB of that for DOOM itself,
# leaving ~300KB for an arena that wants 700KB. See UoomMessages.hpp for the
# mechanism and docs/08-first-boot-debugging.md for the measurements.
option(UOOM_ZONE_IN_SERVICE "Hold DOOM's zone in the service process" ON)

# A smoke build has no DOOM and therefore no arena to hold; leaving this on
# would put a 768K zone in the service alongside any ballast.
if(UOOM_SMOKE)
    set(UOOM_ZONE_IN_SERVICE OFF)
endif()

if(UOOM_ZONE_IN_SERVICE)
    add_compile_definitions(UOOM_ZONE_IN_SERVICE=1)
    set(_una_zone_kb ${UOOM_ZONE_KB_EFFECTIVE})
    # The service is otherwise ~7KB, so its region is the zone plus a little.
    math(EXPR _una_svc_ram "${_una_zone_kb} + 96")
    set(UNA_APP_SERVICE_RAM_LENGTH "${_una_svc_ram}K")
    # And the GUI no longer carries the arena: measured non-zone footprint is
    # 599KB, so 700K is comfortable and stays under the loader's ceiling.
    set(UNA_APP_GUI_RAM_LENGTH "700K")
    # A ladder of zone sizes needs distinct artifact names; the default build
     # keeps the plain one.
    if(UOOM_ZONE_KB)
        set(APP_FILE_NAME "UOOM-z${_una_zone_kb}K")
    endif()
    message(STATUS "UOOM_ZONE_IN_SERVICE: ${_una_zone_kb}K zone in the service process")
else()
    # Single process. Kept buildable for the record, but it does not fit.
    set(APP_FILE_NAME "UOOM-guizone")
    message(WARNING
        "UOOM_ZONE_IN_SERVICE=OFF puts the zone in the GUI process, which the "
        "loader has been measured to refuse. See docs/08-first-boot-debugging.md.")
endif()

if(UOOM_SVC_BALLAST_KB GREATER 0)
    add_compile_definitions(UOOM_SVC_BALLAST_KB=${UOOM_SVC_BALLAST_KB})
    math(EXPR _una_svc_ram "${UOOM_SVC_BALLAST_KB} + 96")
    set(UNA_APP_SERVICE_RAM_LENGTH "${_una_svc_ram}K")
    if(UOOM_BALLAST_KB GREATER 0)
        # Both processes weighed down: each under the per-process ceiling we
        # measured, but well over it combined. This is the pair that tells the
        # two hypotheses apart.
        set(APP_FILE_NAME "UOOM-split${UOOM_BALLAST_KB}g${UOOM_SVC_BALLAST_KB}s")
    else()
        set(APP_FILE_NAME "UOOM-svcballast${UOOM_SVC_BALLAST_KB}K")
    endif()
endif()

if(UOOM_SMOKE)
    add_compile_definitions(UOOM_SMOKE_TEST=1)
    # Same app identity -- you install one *instead of* the other -- but a
    # distinct artifact name, so the .uapp files do not overwrite each other in
    # Output/. una_app_build_app() copies there unconditionally.
    if(UOOM_BALLAST_KB GREATER 0)
        add_compile_definitions(UOOM_BALLAST_KB=${UOOM_BALLAST_KB})
        if(NOT UOOM_SVC_BALLAST_KB GREATER 0)
            set(APP_FILE_NAME "UOOM-ballast${UOOM_BALLAST_KB}K")
        endif()
        # The ballast has to fit inside the region as well as past the loader.
        math(EXPR _una_gui_ram "${UOOM_BALLAST_KB} + 256")
        set(UNA_APP_GUI_RAM_LENGTH "${_una_gui_ram}K")
    else()
        set(UNA_APP_GUI_RAM_LENGTH "256K")
        # Do not clobber the name a service-ballast build already chose.
        if(NOT UOOM_SVC_BALLAST_KB GREATER 0)
            set(APP_FILE_NAME "UOOM-smoke")
        endif()
    endif()
endif()

add_compile_definitions(
    UOOM_ON_WATCH=1
    NORMALUNIX=1
    # doomgeneric's own 32-bit output buffer is bypassed entirely (patch 0001),
    # but these still size s_Fb, so keep them honest.
    DOOMGENERIC_RESX=320
    DOOMGENERIC_RESY=200
)

# DOOM is 1993 C and will not survive -Wall. Silence it there only -- never for
# our own layers, which una-app.cmake already compiles with -Wall.
set_source_files_properties(${DOOM_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

# una-app.cmake compiles everything -Os. The renderer is the exception: it is
# where the frame time lives, and -O2 there is worth the space.
foreach(f ${DOOM_RENDERER_SOURCES})
    set_source_files_properties(${f} PROPERTIES COMPILE_OPTIONS "-w;-O2")
endforeach()

# ------------------------------------------------------------------ targets

set(SERVICE_SOURCES
    ${UOOM_SERVICE_LIB_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_APPSYSTEM}      # the SDK's service main(), which
)                                     # #includes our Service.hpp
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
)
una_app_build_service(${APP_NAME}Service.elf)

set(GUI_SOURCES
    ${UOOM_GUI_GLUE}                  # includes our own main()
    ${UOOM_PORT_SOURCES}
    ${DOOM_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}         # startup, system.cpp, KernelBuilder, Logger
    ${UOOM_SDK_CMDPROC}
    # NOT ${UNA_SDK_SOURCES_GUI}: see UoomMain.cpp
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${DOOM_PATH}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_GUI}
)
una_app_build_gui(${APP_NAME}GUI.elf)

una_app_build_app()
