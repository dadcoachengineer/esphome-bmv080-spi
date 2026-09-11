# ESP-IDF component wrapping Bosch's precompiled BMV080 SDK.
#
# WHY THIS EXISTS (see repo issue: native IDF build)
# --------------------------------------------------
# Until ESPHome 2026.x, ESP-IDF firmware was built through PlatformIO, and
# `cg.add_build_flag("-I...")` / `-L` / `-l` reached the compiler via
# platformio.ini. ESPHome now generates a native ESP-IDF CMake build and does
# NOT emit platformio.ini at all; `CORE.build_flags` is consumed only by
# `esphome/build_gen/platformio.py`, never by `build_gen/espidf.py`.
#
# The practical effect was that every flag this component added was silently
# discarded: the compile failed on `bmv080_defs.h: No such file or directory`
# even though the header was present on disk, and the link would have failed
# next on -l_bmv080 / -l_postProcessor.
#
# Registering the SDK as a real IDF component is the native equivalent and is
# independent of the PlatformIO compatibility layer.
#
# `${IDF_TARGET}` resolves to esp32 / esp32s3 / esp32c3 / esp32c6 ..., which is
# exactly how the per-arch directories are named, so it also replaces the old
# manual arch loop.

idf_component_register(INCLUDE_DIRS ".")

set(BMV080_ARCH_DIR "${CMAKE_CURRENT_LIST_DIR}/${IDF_TARGET}")

if(NOT EXISTS "${BMV080_ARCH_DIR}/lib_bmv080.a")
    # Fail loudly and specifically. The previous failure mode was a missing
    # header 200 lines into a ninja log, which named neither the SDK nor the
    # architecture.
    message(FATAL_ERROR
        "BMV080: no precompiled Bosch SDK for IDF_TARGET='${IDF_TARGET}'. "
        "Expected ${BMV080_ARCH_DIR}/lib_bmv080.a. "
        "Supported: esp32, esp32s3, esp32c3, esp32c6.")
endif()

add_prebuilt_library(bmv080_sdk_lib "${BMV080_ARCH_DIR}/lib_bmv080.a")
add_prebuilt_library(bmv080_sdk_postprocessor "${BMV080_ARCH_DIR}/lib_postProcessor.a")

# INTERFACE: this component registers no sources of its own, so COMPONENT_LIB
# is an interface library and its link deps must be INTERFACE too.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    bmv080_sdk_lib
    bmv080_sdk_postprocessor)
