# Agogica Tracker, the head tracking companion app (target ComposerTracker). tracker/README.md.
# macOS: an app bundle. Windows: a folder with the exe, onnxruntime.dll, ui\ and models\ next
# to each other.

# Compile-only builds (CI): no models are fetched, checked or copied - the app builds but
# can't track.
option(COMPOSER_TRACKER_SKIP_MODELS "Build the Tracker without its models" OFF)

# --- ONNX Runtime (MIT): runs the models; never linked into the plugin ------------------------
# ponytail: macOS arm64 only - ONNX Runtime ships no universal macOS build; Intel Macs need a
# source build.
set(ONNXRUNTIME_VERSION 1.30.0)
include(FetchContent)
if(APPLE)
    FetchContent_Declare(onnxruntime
        URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz
        URL_HASH SHA256=6ebb5062a934537c352937821f9fe9718e7de1a2db1122a93dd363ffd53a7012
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
else()
    FetchContent_Declare(onnxruntime
        URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-win-x64-${ONNXRUNTIME_VERSION}.zip
        URL_HASH SHA256=c6ba983baf5681af108599675d2a89c2d145512d02de28aed0bff177cd0ba949
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()
FetchContent_MakeAvailable(onnxruntime)
set(ONNXRUNTIME_LIB ${onnxruntime_SOURCE_DIR}/lib)

# --- The app --------------------------------------------------------------------------------------
juce_add_gui_app(ComposerTracker
    COMPANY_NAME Agogica
    BUNDLE_ID com.agogica.tracker
    PRODUCT_NAME "Agogica Tracker"
    VERSION ${COMPOSER_VERSION}
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/icon.svg
    CAMERA_PERMISSION_ENABLED TRUE
    CAMERA_PERMISSION_TEXT "Agogica Tracker follows your head with the camera so Composer can keep the ensemble fixed in the room. Video never leaves this Mac.")

target_sources(ComposerTracker PRIVATE
    app/Main.cpp
    app/TrackerPage.cpp
    app/TrackerWindow.cpp
    tracking/CameraFeed.cpp
    $<IF:$<PLATFORM_ID:Darwin>,tracking/CameraSource_mac.mm,tracking/CameraSource_juce.cpp>
    tracking/ProcessCpu.cpp
    tracking/TrackingLoop.cpp
    vision/HeadDetector.cpp
    vision/HeadPoseEstimator.cpp
    vision/ImageTensor.cpp
    vision/OnnxModel.cpp
    ${CMAKE_SOURCE_DIR}/Source/stage/HeadTrackingFile.cpp) # the file shared with the plugin
target_include_directories(ComposerTracker SYSTEM PRIVATE ${onnxruntime_SOURCE_DIR}/include) # third-party warnings off
if(APPLE)
    target_link_libraries(ComposerTracker PRIVATE ${ONNXRUNTIME_LIB}/libonnxruntime.1.dylib
        "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo") # tracking/CameraSource_mac.mm
    set_target_properties(ComposerTracker PROPERTIES BUILD_RPATH "@executable_path/../Frameworks" INSTALL_RPATH "@executable_path/../Frameworks")
else()
    # The exe's own folder is searched first, before System32's Windows ML onnxruntime.dll.
    target_link_libraries(ComposerTracker PRIVATE ${ONNXRUNTIME_LIB}/onnxruntime.lib)
endif()
target_compile_definitions(ComposerTracker PRIVATE
    JUCE_USE_CAMERA=1
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_APPLICATION_VERSION_STRING="${COMPOSER_VERSION}")
target_link_libraries(ComposerTracker
    PRIVATE juce::juce_gui_extra juce::juce_video
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Where the page and the models go (app/AppFiles.h).
if(APPLE)
    set(TRACKER_RESOURCES "$<TARGET_BUNDLE_CONTENT_DIR:ComposerTracker>/Resources")
else()
    set(TRACKER_RESOURCES "$<TARGET_FILE_DIR:ComposerTracker>")
endif()

# --- The page (tracker/ui): files next to the app, not BinaryData ---------------------------------
set(TRACKER_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ui)
set(TRACKER_UI_STAMP ${CMAKE_CURRENT_BINARY_DIR}/tracker-ui.stamp)
file(GLOB_RECURSE TRACKER_UI_SOURCES CONFIGURE_DEPENDS ${TRACKER_UI_DIR}/src/*)

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

# The bundle is filled by POST_BUILD steps, which only run when the app re-links: a new page or
# new models make it re-link (otherwise a page-only change never reaches the bundle).
set(TRACKER_RELINK_ON ${TRACKER_UI_STAMP})

add_custom_command(TARGET ComposerTracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRACKER_RESOURCES}/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${TRACKER_UI_DIR}/dist "${TRACKER_RESOURCES}/ui"
    VERBATIM)

# --- The models: downloaded, not committed (models/MODELS.md); models/models.json lists them -----
if(NOT COMPOSER_TRACKER_SKIP_MODELS)
    set(TRACKER_MODELS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/models)
    file(READ ${TRACKER_MODELS_DIR}/models.json TRACKER_MODELS_JSON)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TRACKER_MODELS_DIR}/models.json)
    string(JSON TRACKER_MODEL_COUNT LENGTH "${TRACKER_MODELS_JSON}")
    math(EXPR TRACKER_LAST_MODEL "${TRACKER_MODEL_COUNT} - 1")
    set(TRACKER_MODEL_FILES)
    foreach(index RANGE ${TRACKER_LAST_MODEL})
        string(JSON name GET "${TRACKER_MODELS_JSON}" ${index} name)
        list(APPEND TRACKER_MODEL_FILES ${TRACKER_MODELS_DIR}/${name})
    endforeach()

    # Fetched only while a file is missing. ponytail: Windows has no python3 by default - the
    # .onnx files are put in tracker/models by hand (the check names a missing one).
    set(TRACKER_CHECK_MODELS ${CMAKE_COMMAND} -P ${TRACKER_MODELS_DIR}/check_models.cmake)
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
    add_custom_target(ComposerTrackerModels DEPENDS ${TRACKER_MODEL_FILES})
    add_dependencies(ComposerTracker ComposerTrackerModels)
    list(APPEND TRACKER_RELINK_ON ${TRACKER_MODEL_FILES})

    # Checked against their pinned hashes as they are copied: no model reaches the app unchecked.
    add_custom_command(TARGET ComposerTracker POST_BUILD
        COMMAND ${TRACKER_CHECK_MODELS}
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${TRACKER_RESOURCES}/models"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${TRACKER_RESOURCES}/models"
        COMMAND ${CMAKE_COMMAND} -E copy ${TRACKER_MODEL_FILES} ${TRACKER_MODELS_DIR}/MODELS.md "${TRACKER_RESOURCES}/models"
        VERBATIM)
endif()

set_target_properties(ComposerTracker PROPERTIES LINK_DEPENDS "${TRACKER_RELINK_ON}")

# --- ONNX Runtime next to the app; the per-user install ------------------------------------------
if(APPLE)
    add_custom_command(TARGET ComposerTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:ComposerTracker>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy ${ONNXRUNTIME_LIB}/libonnxruntime.${ONNXRUNTIME_VERSION}.dylib "$<TARGET_BUNDLE_CONTENT_DIR:ComposerTracker>/Frameworks/libonnxruntime.1.dylib"
        VERBATIM)

    # Installed per user (no admin), like the plugins: Composer's "Start head tracking" starts
    # that copy. Its own target, built by default, so it copies only when the app changed; the
    # release script builds ComposerTracker alone and installs nothing.
    set(TRACKER_INSTALL_STAMP ${CMAKE_CURRENT_BINARY_DIR}/tracker-install.stamp)
    add_custom_command(
        OUTPUT ${TRACKER_INSTALL_STAMP}
        COMMAND /bin/sh -c "rm -rf \"$HOME/Applications/Agogica Tracker.app\" && mkdir -p \"$HOME/Applications\" && ditto \"$<TARGET_BUNDLE_DIR:ComposerTracker>\" \"$HOME/Applications/Agogica Tracker.app\""
        COMMAND ${CMAKE_COMMAND} -E touch ${TRACKER_INSTALL_STAMP}
        DEPENDS ComposerTracker $<TARGET_FILE:ComposerTracker>
        COMMENT "Installing Agogica Tracker to ~/Applications"
        VERBATIM)
    add_custom_target(ComposerTrackerInstall ALL DEPENDS ${TRACKER_INSTALL_STAMP})
else()
    # ponytail: runs from the build folder; the install folder (%LOCALAPPDATA%\Programs\Agogica
    # Tracker) and Composer's startTracker on Windows are still to do.
    add_custom_command(TARGET ComposerTracker POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${ONNXRUNTIME_LIB}/onnxruntime.dll ${ONNXRUNTIME_LIB}/onnxruntime_providers_shared.dll "$<TARGET_FILE_DIR:ComposerTracker>"
        VERBATIM)
endif()
