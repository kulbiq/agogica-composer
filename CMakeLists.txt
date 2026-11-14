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
    Source/dsp/EarlyReflections.cpp
    Source/dsp/ReverbTail.cpp
    Source/dsp/Hrtf.cpp
    Source/licensing/LicenseCheck.cpp)

# Vendored as-is (third_party/monocypher/README): not held to our warning flags. A library of its
# own, since a per-file "-w" after JUCE's /W4 makes MSVC warn about the override (D9025).
add_library(monocypher STATIC third_party/monocypher/monocypher.c third_party/monocypher/monocypher-ed25519.c)
target_compile_options(monocypher PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>)

target_compile_definitions(Composer PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    # Licences signed by the local dev site are accepted by Debug builds only. Explicit, so a
    # configure without a build type can never ship a plugin that trusts the dev key.
    $<$<CONFIG:Debug>:COMPOSER_ACCEPT_DEV_KEY=1>)

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
        monocypher
        juce::juce_audio_utils
        juce::juce_cryptography
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
add_executable(ComposerTests tests/ProcessorTests.cpp tests/GeometryTests.cpp tests/StageTests.cpp tests/DirectPathTests.cpp tests/ReflectionsTests.cpp tests/ReverbTailTests.cpp tests/ProTests.cpp tests/HeadTrackingTests.cpp tests/TrackerTests.cpp tests/LicenseTests.cpp)
target_include_directories(ComposerTests PRIVATE $<TARGET_PROPERTY:Composer,INCLUDE_DIRECTORIES>)
target_compile_definitions(ComposerTests PRIVATE $<TARGET_PROPERTY:Composer,COMPILE_DEFINITIONS>)
target_compile_definitions(ComposerTests PRIVATE COMPOSER_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
target_link_libraries(ComposerTests PRIVATE Composer Catch2::Catch2WithMain)

include(CTest)
include(Catch)
# The fast set (ctest -LE slow) and the slow cases (labelled "slow"); hidden [.x] cases are manual.
catch_discover_tests(ComposerTests TEST_SPEC "~[slow]")
catch_discover_tests(ComposerTests TEST_SPEC "[slow]" PROPERTIES LABELS slow)

# --- Agogica Tracker (head tracking companion app) ---------------------------
# macOS: an app bundle. Windows (Hub plan phase 7b): a folder with the exe, onnxruntime.dll,
# ui\ and models\ next to each other.
option(COMPOSER_TRACKER_DIRECTML "Windows, development: the Tracker runs its models on the GPU through DirectML" OFF)
if(APPLE OR WIN32)
    # ONNX Runtime (MIT) for the tracker's native head-pose engines; never linked into the plugin.
    # ponytail: arm64 only - 1.30 ships no universal macOS build; Intel Macs need a source build.
    include(FetchContent)
    if(APPLE)
        FetchContent_Declare(onnxruntime
            URL https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-osx-arm64-1.30.0.tgz
            URL_HASH SHA256=6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    elseif(COMPOSER_TRACKER_DIRECTML)
        # Development, to measure the GPU (Hub plan 7b): ONNX Runtime's last DirectML build
        # (1.24.4; Microsoft ships none after it) and DirectML itself, as NuGet packages.
        FetchContent_Declare(onnxruntime
            URL https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg
            URL_HASH SHA256=57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_Declare(directml
            URL https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg
            URL_HASH SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(directml)
    else()
        FetchContent_Declare(onnxruntime
            URL https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-win-x64-1.30.0.zip
            URL_HASH SHA256=c6ba983baf5681af108599675d2a89c2d145512d02de28aed0bff177cd0ba949
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    FetchContent_MakeAvailable(onnxruntime)
    # The two archives differ: the release zip has include/ and lib/, the NuGet package
    # build/native/include and runtimes/win-x64/native.
    if(WIN32 AND COMPOSER_TRACKER_DIRECTML)
        set(ONNXRUNTIME_INCLUDE ${onnxruntime_SOURCE_DIR}/build/native/include)
        set(ONNXRUNTIME_LIB ${onnxruntime_SOURCE_DIR}/runtimes/win-x64/native)
    else()
        set(ONNXRUNTIME_INCLUDE ${onnxruntime_SOURCE_DIR}/include)
        set(ONNXRUNTIME_LIB ${onnxruntime_SOURCE_DIR}/lib)
    endif()

    juce_add_gui_app(ComposerTracker
        COMPANY_NAME Agogica
        BUNDLE_ID com.agogica.tracker
        PRODUCT_NAME "Agogica Tracker"
        VERSION ${COMPOSER_VERSION}
        NEEDS_WEB_BROWSER TRUE
        NEEDS_WEBVIEW2 TRUE
        ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/tracker/icon.svg
        CAMERA_PERMISSION_ENABLED TRUE
        CAMERA_PERMISSION_TEXT "Agogica Tracker follows your head with the camera so Composer can keep the ensemble fixed in the room. Video never leaves this Mac.")

    target_sources(ComposerTracker PRIVATE
        tracker/Main.cpp
        tracker/EngineHost.cpp
        tracker/engines/Engines.cpp
        tracker/engines/Onnx.cpp
        tracker/ProcessCpu.cpp
        Source/stage/HeadTrackingFile.cpp)
    target_include_directories(ComposerTracker SYSTEM PRIVATE ${ONNXRUNTIME_INCLUDE}) # third-party warnings off
    if(APPLE)
        target_link_libraries(ComposerTracker PRIVATE ${ONNXRUNTIME_LIB}/libonnxruntime.1.dylib)
        set_target_properties(ComposerTracker PROPERTIES BUILD_RPATH "@executable_path/../Frameworks" INSTALL_RPATH "@executable_path/../Frameworks")
    else()
        # The exe's own folder is searched first, before System32's Windows ML onnxruntime.dll.
        target_link_libraries(ComposerTracker PRIVATE ${ONNXRUNTIME_LIB}/onnxruntime.lib)
    endif()
    target_compile_definitions(ComposerTracker PRIVATE
        $<$<BOOL:${COMPOSER_TRACKER_DIRECTML}>:COMPOSER_TRACKER_DIRECTML=1>
        JUCE_USE_CAMERA=1
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
        JUCE_APPLICATION_VERSION_STRING="${COMPOSER_VERSION}")
    target_link_libraries(ComposerTracker
        PRIVATE juce::juce_gui_extra juce::juce_video
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # The tracker page (MediaPipe + model, ~25 MB) ships as files in the app bundle, not as
    # BinaryData. The model is downloaded once by tracker/ui/scripts/copy-assets.mjs.
    set(TRACKER_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tracker/ui)
    set(TRACKER_UI_STAMP ${CMAKE_CURRENT_BINARY_DIR}/tracker-ui.stamp)
    file(GLOB_RECURSE TRACKER_UI_SOURCES CONFIGURE_DEPENDS ${TRACKER_UI_DIR}/src/* ${TRACKER_UI_DIR}/scripts/*)

    composer_npm_ci(${TRACKER_UI_DIR})
    add_custom_command(
        OUTPUT ${TRACKER_UI_STAMP}
        COMMAND ${NPM} run build
        COMMAND ${CMAKE_COMMAND} -E touch ${TRACKER_UI_STAMP}
        WORKING_DIRECTORY ${TRACKER_UI_DIR}
        DEPENDS ${TRACKER_UI_SOURCES} ${TRACKER_UI_DIR}/index.html ${TRACKER_UI_DIR}/node_modules/.package-lock.json
            ${TRACKER_UI_DIR}/vite.config.ts ${UI_SOURCES}
        COMMENT "Building tracker UI"
        VERBATIM)
    add_custom_target(ComposerTrackerUI DEPENDS ${TRACKER_UI_STAMP})
    # The page imports Composer's fonts and tokens, which need ui/node_modules (npm ci in the
    # Composer UI build): without this a clean tree builds the tracker page first and fails.
    add_dependencies(ComposerTrackerUI ComposerData)
    add_dependencies(ComposerTracker ComposerTrackerUI)

    # The native engines' models (downloaded, not committed: tracker/models/MODELS.md).
    set(TRACKER_MODELS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tracker/models)
    set(TRACKER_MODELS_STAMP ${CMAKE_CURRENT_BINARY_DIR}/tracker-models.stamp)
    set(TRACKER_MODEL_FILES ${TRACKER_MODELS_DIR}/yolox_n_body_head_hand_192x320.onnx
        ${TRACKER_MODELS_DIR}/sixdrepnet360_1x3x224x224_full.onnx)
    set(TRACKER_CHECK_MODELS ${CMAKE_COMMAND} -P ${TRACKER_MODELS_DIR}/check_models.cmake)
    # Fetched only while a file is missing. ponytail: Windows has no python3 by default - the two
    # .onnx files are put in tracker/models by hand (the check names a missing one); CI needs a
    # fetch step of its own.
    if(WIN32)
        set(TRACKER_FETCH_MODELS ${TRACKER_CHECK_MODELS})
    else()
        set(TRACKER_FETCH_MODELS python3 ${TRACKER_MODELS_DIR}/fetch_models.py)
    endif()
    add_custom_command(
        OUTPUT ${TRACKER_MODEL_FILES}
        COMMAND ${TRACKER_FETCH_MODELS}
        COMMENT "Fetching tracker models"
        VERBATIM)
    # Checked again whenever a file is replaced (hashing both takes ~0.7 s, too long for every
    # build). A file copied in with an older date isn't seen as replaced: the POST_BUILD copy
    # below checks them too, so no model reaches the app unchecked.
    add_custom_command(
        OUTPUT ${TRACKER_MODELS_STAMP}
        COMMAND ${TRACKER_CHECK_MODELS}
        COMMAND ${CMAKE_COMMAND} -E touch ${TRACKER_MODELS_STAMP}
        DEPENDS ${TRACKER_MODEL_FILES} ${TRACKER_MODELS_DIR}/check_models.cmake
        COMMENT "Checking tracker models"
        VERBATIM)
    add_custom_target(ComposerTrackerModels DEPENDS ${TRACKER_MODELS_STAMP})
    add_dependencies(ComposerTracker ComposerTrackerModels)

    # The bundle is filled by POST_BUILD steps, which only run when the app re-links: make a new
    # page or new models re-link it (otherwise a page-only change never reaches the bundle).
    set_target_properties(ComposerTracker PROPERTIES LINK_DEPENDS "${TRACKER_UI_STAMP};${TRACKER_MODELS_STAMP}")

    # Where the page and models go (tracker/Main.cpp `resources()`).
    if(APPLE)
        set(TRACKER_RESOURCES "$<TARGET_BUNDLE_CONTENT_DIR:ComposerTracker>/Resources")
    else()
        set(TRACKER_RESOURCES "$<TARGET_FILE_DIR:ComposerTracker>")
    endif()
    add_custom_command(TARGET ComposerTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRACKER_RESOURCES}/ui"
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${TRACKER_UI_DIR}/dist "${TRACKER_RESOURCES}/ui"
        COMMAND ${TRACKER_CHECK_MODELS}
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRACKER_RESOURCES}/models"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TRACKER_RESOURCES}/models"
        COMMAND ${CMAKE_COMMAND} -E copy ${TRACKER_MODEL_FILES} ${TRACKER_MODELS_DIR}/MODELS.md "${TRACKER_RESOURCES}/models"
        VERBATIM)

    if(APPLE)
        add_custom_command(TARGET ComposerTracker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ComposerTracker>/Frameworks"
            COMMAND ${CMAKE_COMMAND} -E copy ${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.1.30.0.dylib "$<TARGET_BUNDLE_CONTENT_DIR:ComposerTracker>/Frameworks/libonnxruntime.1.dylib"
            VERBATIM)

        # Install per user (no admin), like the plugins: Composer starts it from there.
        set(TRACKER_INSTALL_DIR "$ENV{HOME}/Applications/Agogica Tracker.app")
        add_custom_command(TARGET ComposerTracker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRACKER_INSTALL_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:ComposerTracker>" "${TRACKER_INSTALL_DIR}"
            VERBATIM)
    else()
        # ponytail: runs from the build folder; the install folder (%LOCALAPPDATA%\Programs\Agogica
        # Tracker) and Composer's startTracker come with phase 7b proper.
        set(TRACKER_DLLS ${ONNXRUNTIME_LIB}/onnxruntime.dll ${ONNXRUNTIME_LIB}/onnxruntime_providers_shared.dll)
        if(COMPOSER_TRACKER_DIRECTML)
            list(APPEND TRACKER_DLLS ${directml_SOURCE_DIR}/bin/x64-win/DirectML.dll)
        endif()
        add_custom_command(TARGET ComposerTracker POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${TRACKER_DLLS} "$<TARGET_FILE_DIR:ComposerTracker>"
            VERBATIM)
    endif()
endif()
