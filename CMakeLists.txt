cmake_minimum_required(VERSION 3.22)
project(MANDELBROT VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------------------
# JUCE: use -DJUCE_DIR=<path>, else ./JUCE, else ./JUCE_local, else download.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE_local/CMakeLists.txt")
    add_subdirectory(JUCE_local EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 654bdbc7a9db0dae90849b44abe91f339e9f0768   # JUCE 9.0.6 (tested revision)
        GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MANDEL_CORE_SOURCES
    Source/Core/Identifiers.h
    Source/Core/Scales.h
    Source/Core/Scales.cpp
    Source/Core/Params.h
    Source/Core/Params.cpp
    Source/Core/ProjectModel.h
    Source/Core/ProjectModel.cpp
    Source/Fractal/MandelMath.h
    Source/Fractal/MandelMath.cpp
    Source/Generative/Generator.h
    Source/Generative/Generator.cpp
    Source/Audio/DSP.h
    Source/Audio/Effects.h
    Source/Audio/Effects.cpp
    Source/Audio/Synth.h
    Source/Audio/Synth.cpp
    Source/Audio/DrumMachine.h
    Source/Audio/DrumMachine.cpp
    Source/Audio/AudioPool.h
    Source/Audio/AudioPool.cpp
    Source/Audio/Snapshot.h
    Source/Audio/Snapshot.cpp
    Source/Audio/Engine.h
    Source/Audio/Engine.cpp
    Source/Audio/Exporter.h
    Source/Audio/Exporter.cpp
    Source/Live/LiveSequencer.h
    Source/Live/LiveSequencer.cpp
)

set(MANDEL_UI_SOURCES
    Source/Main.cpp
    Source/UI/Theme.h
    Source/UI/Theme.cpp
    Source/UI/Widgets.h
    Source/UI/Widgets.cpp
    Source/UI/AppState.h
    Source/UI/Composer.h
    Source/UI/Composer.cpp
    Source/UI/MainComponent.h
    Source/UI/MainComponent.cpp
    Source/UI/TransportBar.h
    Source/UI/TransportBar.cpp
    Source/UI/ArrangementView.h
    Source/UI/ArrangementView.cpp
    Source/UI/FractalView.h
    Source/UI/FractalView.cpp
    Source/UI/PianoRoll.h
    Source/UI/PianoRoll.cpp
    Source/UI/GeneratorPage.h
    Source/UI/GeneratorPage.cpp
    Source/UI/SynthPage.h
    Source/UI/SynthPage.cpp
    Source/UI/DrumPage.h
    Source/UI/DrumPage.cpp
    Source/UI/MixerPage.h
    Source/UI/MixerPage.cpp
    Source/UI/AudioEditorPage.h
    Source/UI/AudioEditorPage.cpp
    Source/UI/LivePage.h
    Source/UI/LivePage.cpp
)

set(MANDEL_JUCE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics)

set(MANDEL_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="MANDELBROT"
    JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_ASIO=0
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1)

if(UNIX AND NOT APPLE)
    # Linux dev/test builds (headless CI): no ALSA/JACK headers required.
    option(MANDEL_LINUX_NO_AUDIO "Build Linux without ALSA/JACK" ON)
    if(MANDEL_LINUX_NO_AUDIO)
        list(APPEND MANDEL_DEFS JUCE_ALSA=0 JUCE_JACK=0)
    endif()
    list(APPEND MANDEL_DEFS JUCE_USE_XINPUT=0)
endif()

# ---------------- Application ----------------
juce_add_gui_app(Mandelbrot
    PRODUCT_NAME "MANDELBROT"
    COMPANY_NAME "Mandelbrot Audio"
    BUNDLE_ID com.mandelbrotaudio.mandelbrot
    VERSION ${PROJECT_VERSION})

target_sources(Mandelbrot PRIVATE ${MANDEL_CORE_SOURCES} ${MANDEL_UI_SOURCES})
target_include_directories(Mandelbrot PRIVATE Source)
target_compile_definitions(Mandelbrot PRIVATE ${MANDEL_DEFS})
target_link_libraries(Mandelbrot PRIVATE ${MANDEL_JUCE_MODULES}
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(Mandelbrot PRIVATE /MP /bigobj)
endif()

# ---------------- Tests (console) ----------------
juce_add_console_app(MandelbrotTests PRODUCT_NAME "MandelbrotTests")
set(MANDEL_TEST_UI_SOURCES ${MANDEL_UI_SOURCES})
list(REMOVE_ITEM MANDEL_TEST_UI_SOURCES Source/Main.cpp)
target_sources(MandelbrotTests PRIVATE ${MANDEL_CORE_SOURCES} ${MANDEL_TEST_UI_SOURCES} Tests/Tests.cpp)
target_include_directories(MandelbrotTests PRIVATE Source)
target_compile_definitions(MandelbrotTests PRIVATE ${MANDEL_DEFS} MANDEL_TESTS=1)
target_link_libraries(MandelbrotTests PRIVATE ${MANDEL_JUCE_MODULES}
    juce::juce_recommended_config_flags)
if(MSVC)
    target_compile_options(MandelbrotTests PRIVATE /MP /bigobj)
endif()
