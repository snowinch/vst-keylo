cmake_minimum_required(VERSION 3.22)
project(KeyloVST VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# ── JUCE ──────────────────────────────────────────────────────────────────────
add_subdirectory(JUCE)

# ── aubio static lib (minimal: no IO, no FFTW, no sndfile) ───────────────────
set(AUBIO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/aubio/src)

set(AUBIO_SOURCES
    ${AUBIO_SRC_DIR}/fvec.c
    ${AUBIO_SRC_DIR}/fmat.c
    ${AUBIO_SRC_DIR}/cvec.c
    ${AUBIO_SRC_DIR}/lvec.c
    ${AUBIO_SRC_DIR}/mathutils.c
    ${AUBIO_SRC_DIR}/musicutils.c
    ${AUBIO_SRC_DIR}/vecutils.c
    # utils
    ${AUBIO_SRC_DIR}/utils/log.c
    ${AUBIO_SRC_DIR}/utils/hist.c
    ${AUBIO_SRC_DIR}/utils/scale.c
    ${AUBIO_SRC_DIR}/utils/parameter.c
    ${AUBIO_SRC_DIR}/utils/strutils.c
    # temporal filters
    ${AUBIO_SRC_DIR}/temporal/biquad.c
    ${AUBIO_SRC_DIR}/temporal/filter.c
    ${AUBIO_SRC_DIR}/temporal/a_weighting.c
    ${AUBIO_SRC_DIR}/temporal/c_weighting.c
    # spectral (ooura FFT only, no FFTW)
    ${AUBIO_SRC_DIR}/spectral/ooura_fft8g.c
    ${AUBIO_SRC_DIR}/spectral/dct_plain.c
    ${AUBIO_SRC_DIR}/spectral/dct_ooura.c
    ${AUBIO_SRC_DIR}/spectral/dct.c
    ${AUBIO_SRC_DIR}/spectral/fft.c
    ${AUBIO_SRC_DIR}/spectral/phasevoc.c
    ${AUBIO_SRC_DIR}/spectral/filterbank.c
    ${AUBIO_SRC_DIR}/spectral/filterbank_mel.c
    ${AUBIO_SRC_DIR}/spectral/mfcc.c
    ${AUBIO_SRC_DIR}/spectral/specdesc.c
    ${AUBIO_SRC_DIR}/spectral/statistics.c
    ${AUBIO_SRC_DIR}/spectral/awhitening.c
    ${AUBIO_SRC_DIR}/spectral/tss.c
    # pitch
    ${AUBIO_SRC_DIR}/pitch/pitch.c
    ${AUBIO_SRC_DIR}/pitch/pitchyin.c
    ${AUBIO_SRC_DIR}/pitch/pitchyinfast.c
    ${AUBIO_SRC_DIR}/pitch/pitchyinfft.c
    ${AUBIO_SRC_DIR}/pitch/pitchfcomb.c
    ${AUBIO_SRC_DIR}/pitch/pitchschmitt.c
    ${AUBIO_SRC_DIR}/pitch/pitchmcomb.c
    ${AUBIO_SRC_DIR}/pitch/pitchspecacf.c
    # tempo / beat
    ${AUBIO_SRC_DIR}/tempo/tempo.c
    ${AUBIO_SRC_DIR}/tempo/beattracking.c
    # onset
    ${AUBIO_SRC_DIR}/onset/onset.c
    ${AUBIO_SRC_DIR}/onset/peakpicker.c
    # notes
    ${AUBIO_SRC_DIR}/notes/notes.c
    # synth (needed by notes deps)
    ${AUBIO_SRC_DIR}/synth/sampler.c
    ${AUBIO_SRC_DIR}/synth/wavetable.c
)

add_library(aubio_static STATIC ${AUBIO_SOURCES})

# Generate config.h where aubio_priv.h can find it as "config.h"
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/aubio_gen)
configure_file(cmake/aubio_config.h ${CMAKE_CURRENT_BINARY_DIR}/aubio_gen/config.h COPYONLY)

target_include_directories(aubio_static PUBLIC  ${AUBIO_SRC_DIR})
target_include_directories(aubio_static PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/aubio_gen)
# aubio internally uses HAVE_CONFIG_H to include config.h
target_compile_definitions(aubio_static PRIVATE HAVE_CONFIG_H)
if(MSVC)
    target_compile_options(aubio_static PRIVATE /W0 /D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(aubio_static PRIVATE -w)
endif()

# ── ONNX Runtime ─────────────────────────────────────────────────────────────
# macOS:   brew install onnxruntime  (auto-detected)
# Windows: pass -DORT_ROOT=<path to prebuilt SDK> to cmake
# Linux:   pass -DORT_ROOT=<path to prebuilt SDK> to cmake
if(NOT DEFINED ORT_ROOT)
    foreach(prefix /opt/homebrew /usr/local)
        if(EXISTS "${prefix}/opt/onnxruntime")
            set(ORT_ROOT "${prefix}/opt/onnxruntime")
            break()
        endif()
    endforeach()
endif()

find_library(ORT_LIB onnxruntime
    HINTS "${ORT_ROOT}/lib" /opt/homebrew/lib /usr/local/lib
    REQUIRED)

find_path(ORT_INCLUDE onnxruntime_cxx_api.h
    HINTS
        "${ORT_ROOT}/include"                              # Windows prebuilt
        "${ORT_ROOT}/include/onnxruntime/core/session"
        "${ORT_ROOT}/include/onnxruntime"
        /opt/homebrew/include/onnxruntime/core/session
        /opt/homebrew/include/onnxruntime
        /opt/homebrew/Cellar/onnxruntime/1.26.0_1/include/onnxruntime
        /usr/local/include/onnxruntime/core/session
        /usr/local/include/onnxruntime
    REQUIRED)

message(STATUS "ONNX Runtime lib:     ${ORT_LIB}")
message(STATUS "ONNX Runtime include: ${ORT_INCLUDE}")

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(Keylo
    PLUGIN_NAME             "Keylo"
    PRODUCT_NAME            "Keylo"
    COMPANY_NAME            "Snowinch"
    BUNDLE_ID               "com.snowinch.keylo"
    PLUGIN_MANUFACTURER_CODE Snwi
    PLUGIN_CODE             Kylo
    FORMATS                 VST3 AU Standalone
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         "Analyzer"
    AU_MAIN_TYPE            kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(Keylo)

# ── Embedded assets (fonts + ONNX model) ─────────────────────────────────────
juce_add_binary_data(KeyloAssets SOURCES
    Assets/fonts/Panchang-Bold.ttf
    Assets/skey.onnx
)
set_target_properties(KeyloAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_sources(Keylo PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analyser.cpp
    Source/DSP/BPMDetector.cpp
    Source/DSP/KeyDetector.cpp
    Source/DSP/PitchDetector.cpp
    Source/DSP/SkeyInference.cpp
)

target_include_directories(Keylo PRIVATE Source ${AUBIO_SRC_DIR} ${ORT_INCLUDE})

target_compile_definitions(Keylo PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    $<$<CXX_COMPILER_ID:MSVC>:_USE_MATH_DEFINES>
)

target_link_libraries(Keylo PRIVATE
    aubio_static
    KeyloAssets
    ${ORT_LIB}
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Windows: delay-load onnxruntime.dll so the host can load Keylo.vst3 even
# before onnxruntime.dll is on the search path. We load it explicitly at
# runtime via LoadLibraryW with the full path (see PluginProcessor.cpp).
if(WIN32)
    target_link_options(Keylo PRIVATE /DELAYLOAD:onnxruntime.dll)
    target_link_libraries(Keylo PRIVATE delayimp)
endif()

# ── Test executable (no JUCE — pure DSP + aubio) ──────────────────────────────
add_executable(KeyloTest
    tests/test_key.cpp
    Source/DSP/KeyDetector.cpp
    Source/DSP/BPMDetector.cpp
    Source/DSP/PitchDetector.cpp
)
target_include_directories(KeyloTest PRIVATE Source ${AUBIO_SRC_DIR})
target_link_libraries(KeyloTest PRIVATE aubio_static)
if(MSVC)
    target_compile_options(KeyloTest PRIVATE /O2 /D_CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(KeyloTest PRIVATE -O2)
endif()
