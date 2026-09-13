# Copy the MSVC C/C++ runtime redistributable DLLs next to lyra.exe.
# Invoked from CMakeLists.txt POST_BUILD.  Requires VCToolsRedistDir
# (set by vcvars64.bat).  No-op with a warning if that env var is missing
# so a non-MSVC generator still configures.
if(NOT DEFINED ENV{VCToolsRedistDir} OR "$ENV{VCToolsRedistDir}" STREQUAL "")
    message(WARNING "VCToolsRedistDir is not set — skipping app-local MSVC CRT. "
                    "Run the build from a VS developer prompt (vcvars64.bat) "
                    "so the installer can launch on PCs without VC++ Redistributable.")
    return()
endif()

if(NOT DEFINED DEST_DIR OR DEST_DIR STREQUAL "")
    message(FATAL_ERROR "copy_msvc_crt.cmake requires -DDEST_DIR=<dir>")
endif()
# Strip accidental surrounding quotes from the generator expression.
string(REPLACE "\"" "" DEST_DIR "${DEST_DIR}")

file(GLOB _crt_dlls "$ENV{VCToolsRedistDir}/x64/Microsoft.VC*.CRT/*.dll")
if(NOT _crt_dlls)
    message(WARNING "No MSVC CRT DLLs under $ENV{VCToolsRedistDir}/x64/Microsoft.VC*.CRT/")
    return()
endif()

file(COPY ${_crt_dlls} DESTINATION "${DEST_DIR}")
