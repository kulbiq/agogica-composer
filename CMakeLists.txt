cmake_minimum_required(VERSION 3.22)

# Version comes from the latest vX.Y.Z git tag; untagged checkouts build as 0.1.0.
execute_process(
    COMMAND git describe --tags --match "v[0-9]*" --abbrev=0
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE COMPOSER_GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

if(COMPOSER_GIT_TAG MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
    set(COMPOSER_VERSION ${CMAKE_MATCH_1})
else()
    set(COMPOSER_VERSION 0.1.0)
endif()

project(Composer VERSION ${COMPOSER_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# build/<preset>/compile_commands.json: the editor's IntelliSense reads the real flags and include
# paths (JUCE modules, ONNX Runtime, BinaryData) from it (.vscode/settings.json).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static MSVC runtime: Windows testers need no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Sources are UTF-8 (degree signs, +-, dashes in comments and test messages); without this
# MSVC reads them in the system code page (C4819, mangled strings).
if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(JUCE)

# Install into per-user plugin folders (no admin). macOS defaults are already
# ~/Library/Audio/Plug-Ins/{VST3,Components}.
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" local_app_data)
    set_directory_properties(PROPERTIES JUCE_VST3_COPY_DIR "${local_app_data}/Programs/Common/VST3")
endif()

juce_add_plugin(Composer
    COMPANY_NAME Agogica
    BUNDLE_ID com.agogica.composer
    PLUGIN_MANUFACTURER_CODE Agog
    PLUGIN_CODE Cmps
    PRODUCT_NAME "Composer"
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Composer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/bridge/Stage.cpp
    Source/stage/StageRegistry.cpp
    Source/stage/HeadTracking.cpp
    Source/stage/HeadTrackingFile.cpp
    Source/dsp/DirectPath.cpp
    Source/dsp/Placer.cpp
    Source/dsp/Hrtf.cpp
    Source/dsp/Fir.cpp)

target_compile_definitions(Composer PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)

# Debug only: load the UI from `npm run dev` instead of the embedded zip.
option(COMPOSER_UI_DEV_SERVER "Debug builds load the web UI from the Vite dev server" OFF)

if(COMPOSER_UI_DEV_SERVER)
    target_compile_definitions(Composer PRIVATE
        $<$<CONFIG:Debug>:COMPOSER_UI_DEV_SERVER_URL="http://localhost:5173/">)
endif()

# --- Embedded data: web UI (Vite build -> zip) + HRIR tables ----------------
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui)
set(UI_ZIP ${CMAKE_CURRENT_BINARY_DIR}/ui.zip)

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS ${UI_DIR}/src/*)

# npm is a batch file on Windows: run through cmd, or Visual Studio's build script ends after it.
set(NPM npm)
if(WIN32)
    set(NPM cmd /c npm)
endif()

# npm ci only when the lockfile changes (it wipes and reinstalls node_modules: slow). npm writes
# node_modules/.package-lock.json, so a deleted node_modules installs again too.
function(composer_npm_ci dir)
    add_custom_command(
        OUTPUT ${dir}/node_modules/.package-lock.json
        COMMAND ${NPM} ci --no-audit --no-fund
        COMMAND ${CMAKE_COMMAND} -E touch ${dir}/node_modules/.package-lock.json
        WORKING_DIRECTORY ${dir}
        DEPENDS ${dir}/package.json ${dir}/package-lock.json
        COMMENT "Installing packages in ${dir}"
        VERBATIM)
endfunction()
composer_npm_ci(${UI_DIR})

add_custom_command(
    OUTPUT ${UI_ZIP}
    COMMAND ${NPM} run build
    # Zip from inside dist/ so entries are root-level (index.html, assets/...).
    COMMAND ${CMAKE_COMMAND} -E chdir ${UI_DIR}/dist ${CMAKE_COMMAND} -E tar cf ${UI_ZIP} --format=zip .
    WORKING_DIRECTORY ${UI_DIR}
    DEPENDS ${UI_SOURCES}
        ${UI_DIR}/index.html
        ${UI_DIR}/node_modules/.package-lock.json
        ${UI_DIR}/vite.config.ts
        ${UI_DIR}/tsconfig.json
    COMMENT "Building web UI"
    VERBATIM)

# The web UI zip and the binaural HRIR tables (see Resources/hrir/NOTICE).
file(GLOB HRIR_TABLES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Resources/hrir/*.bin)
juce_add_binary_data(ComposerData SOURCES ${UI_ZIP} ${HRIR_TABLES})

target_link_libraries(Composer
    PRIVATE
        ComposerData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Tests ------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# Tests link the plugin's shared code (which already contains the JUCE modules)
# and borrow its include paths and definitions instead of compiling JUCE again.
add_executable(ComposerTests tests/ProcessorTests.cpp tests/GeometryTests.cpp tests/StageTests.cpp tests/DirectPathTests.cpp tests/BlockDspTests.cpp tests/ProTests.cpp tests/HeadTrackingTests.cpp tests/TrackerTests.cpp tests/ListenerPoseTests.cpp
    tracker/vision/ImageTensor.cpp) # the tracker's pure code, tested here (the app itself needs ONNX Runtime)
target_include_directories(ComposerTests PRIVATE $<TARGET_PROPERTY:Composer,INCLUDE_DIRECTORIES>)
target_compile_definitions(ComposerTests PRIVATE $<TARGET_PROPERTY:Composer,COMPILE_DEFINITIONS>)
target_compile_definitions(ComposerTests PRIVATE COMPOSER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(ComposerTests PRIVATE Composer Catch2::Catch2WithMain)

include(CTest)
include(Catch)
# The fast set (ctest -LE slow) and the slow cases (labelled "slow"); hidden [.x] cases are manual.
catch_discover_tests(ComposerTests TEST_SPEC "~[slow]")
catch_discover_tests(ComposerTests TEST_SPEC "[slow]" PROPERTIES LABELS slow)

# --- Agogica Tracker (head tracking companion app): tracker/CMakeLists.txt ----
if(APPLE OR WIN32)
    add_subdirectory(tracker)
endif()
