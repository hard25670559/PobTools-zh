# PobTools' triplet for Apple Silicon.
#
# Identical to vcpkg's own arm64-osx except for one port: ANGLE is built as a
# shared library.
#
# The launcher asks GLFW for an OpenGL ES context through EGL
# (GLFW_CONTEXT_CREATION_API = GLFW_EGL_CONTEXT_API, see host/launcher_ui.cpp),
# which is how it gets the same renderer on every platform. GLFW's EGL backend
# finds that implementation with dlopen at runtime, so a static ANGLE cannot
# satisfy it: the symbols are inside the executable, and there is no library
# file to open. Window creation then fails with nothing in the log to explain
# why, which is what this triplet exists to prevent.
#
# Everything else stays static, which is what keeps the bundle self-contained.

set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

if (PORT STREQUAL "angle")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif ()
