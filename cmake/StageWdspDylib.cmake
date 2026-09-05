# Lyra — stage libwdsp.dylib next to the built executable (macOS).
#
# Run at build time from the POST_BUILD step in the top-level CMakeLists.
# A missing dylib is a warning, not an error: the app still links, and
# WdspNative::load() reports the absence with a proper operator-facing
# message telling them to run tools/build_wdsp_macos.sh.

if(NOT DEFINED SOURCE_DYLIB OR NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "StageWdspDylib.cmake requires -DSOURCE_DYLIB and -DDEST_DIR")
endif()

if(EXISTS "${SOURCE_DYLIB}")
    file(COPY "${SOURCE_DYLIB}" DESTINATION "${DEST_DIR}")
    message(STATUS "Staged WDSP engine: ${SOURCE_DYLIB}")
else()
    message(WARNING
        "WDSP engine not found at ${SOURCE_DYLIB}\n"
        "   Lyra will build but cannot start DSP until you run:\n"
        "       tools/build_wdsp_macos.sh")
endif()
