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
    Source/dsp/Spatializer.cpp)

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

# --- Web UI: Vite build -> zip -> BinaryData --------------------------------
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui)
set(UI_ZIP ${CMAKE_CURRENT_BINARY_DIR}/ui.zip)

file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS ${UI_DIR}/src/*)

add_custom_command(
    OUTPUT ${UI_ZIP}
    COMMAND npm ci --no-audit --no-fund
    COMMAND npm run build
    # Zip from inside dist/ so entries are root-level (index.html, assets/...).
    COMMAND ${CMAKE_COMMAND} -E chdir ${UI_DIR}/dist ${CMAKE_COMMAND} -E tar cf ${UI_ZIP} --format=zip .
    WORKING_DIRECTORY ${UI_DIR}
    DEPENDS ${UI_SOURCES}
        ${UI_DIR}/index.html
        ${UI_DIR}/package.json
        ${UI_DIR}/package-lock.json
        ${UI_DIR}/vite.config.ts
        ${UI_DIR}/tsconfig.json
    COMMENT "Building web UI"
    VERBATIM)

juce_add_binary_data(ComposerWebUI SOURCES ${UI_ZIP})

target_link_libraries(Composer
    PRIVATE
        ComposerWebUI
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
add_executable(ComposerTests tests/ProcessorTests.cpp tests/GeometryTests.cpp tests/StageTests.cpp tests/SpatializerTests.cpp)
target_include_directories(ComposerTests PRIVATE $<TARGET_PROPERTY:Composer,INCLUDE_DIRECTORIES>)
target_compile_definitions(ComposerTests PRIVATE $<TARGET_PROPERTY:Composer,COMPILE_DEFINITIONS>)
target_link_libraries(ComposerTests PRIVATE Composer Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(ComposerTests)
