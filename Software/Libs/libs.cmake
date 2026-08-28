# Provides LIBS_SOURCES / LIBS_INCLUDE_DIRS, the variables the SDK's example
# apps expect an app's Software/Libs to define.
#
# The SDK's own examples glob Sources/*.c*. UOOM lists explicitly instead,
# because its GUI and service link different subsets: the service needs only
# Service.cpp, while the port layers and the UNA adapter belong to the GUI.
# A glob would put the platform adapter -- which calls KernelProviderGUI -- into
# the service ELF, where that provider is never created.

set(LIBS_INCLUDE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/Header
)

# Service side: just the supervisor.
set(UOOM_SERVICE_LIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/Sources/Service.cpp
)

# GUI side: the platform-free port layers (these are what tests/run.sh builds
# on the host) plus the two UNA adapters.
# The entry point: the DOOM backend, or the platform-only bring-up.
if(UOOM_SMOKE)
    set(UOOM_ENTRY ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_smoke.c)
else()
    set(UOOM_ENTRY ${CMAKE_CURRENT_LIST_DIR}/Sources/doomgeneric_uoom.c)
endif()

set(UOOM_PORT_SOURCES
    ${UOOM_ENTRY}
    ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_video.c
    ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_input.c
    ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_text.c
    ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_file.c
    ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_sys.c
    ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_libc.c
)

set(UOOM_GUI_GLUE
    ${CMAKE_CURRENT_LIST_DIR}/Sources/UoomMain.cpp
    ${CMAKE_CURRENT_LIST_DIR}/Sources/uoom_una_platform.cpp
)

# Kept for compatibility with the SDK's naming; not used directly.
set(LIBS_SOURCES ${UOOM_SERVICE_LIB_SOURCES})
