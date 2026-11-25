cmake_minimum_required(VERSION 3.22)
set(VERSEB_VERSION "0.1.0" CACHE STRING "Version embedded in the plugin")
project(Verseb VERSION ${VERSEB_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
include(CTest)
option(VERSEB_BUILD_PREVIEW "Build the interactive UI preview" ON)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG dabd5ad13cd869cba41046947fffc8e5ce5cd662)
FetchContent_MakeAvailable(JUCE)
set(formats VST3 Standalone)
if(APPLE)
    list(APPEND formats AU)
endif()
set(font_licenses assets/fonts/barlow-OFL.txt assets/fonts/ibmplexmono-OFL.txt)
juce_add_binary_data(VersebAssets SOURCES assets/wallpaper.jpg
    assets/fonts/Barlow-Bold.ttf assets/fonts/Barlow-SemiBold.ttf
    assets/fonts/IBMPlexMono-Regular.ttf assets/fonts/IBMPlexMono-Medium.ttf)
juce_add_plugin(Verseb
    COMPANY_NAME "Verseb" BUNDLE_ID "audio.verseb.reverse"
    PLUGIN_MANUFACTURER_CODE Vrsb PLUGIN_CODE Vsb1
    FORMATS ${formats} PRODUCT_NAME "Verseb"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Verseb captura la entrada de audio seleccionada para generar la reverb inversa."
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Reverb)
target_sources(Verseb PRIVATE Source/Renderer.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(Verseb PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Verseb PRIVATE VersebAssets juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(APPLE)
    foreach(format IN LISTS formats)
        target_sources(Verseb_${format} PRIVATE ${font_licenses})
    endforeach()
    set_source_files_properties(${font_licenses} PROPERTIES
        HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION Resources/licenses)
endif()
if(BUILD_TESTING)
    add_executable(verseb_tests Tests/RendererTests.cpp)
    target_include_directories(verseb_tests PRIVATE Source $<TARGET_PROPERTY:Verseb,INCLUDE_DIRECTORIES>)
    target_compile_definitions(verseb_tests PRIVATE $<TARGET_PROPERTY:Verseb,COMPILE_DEFINITIONS>)
    target_link_libraries(verseb_tests PRIVATE Verseb)
    add_test(NAME render_and_state COMMAND verseb_tests)
endif()
if(VERSEB_BUILD_PREVIEW)
    add_executable(VersebPreview WIN32 MACOSX_BUNDLE)
    set_target_properties(VersebPreview PROPERTIES OUTPUT_NAME "Verseb Preview"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/VersebPreview_artefacts/$<CONFIG>"
        MACOSX_BUNDLE_GUI_IDENTIFIER "audio.verseb.preview")
    target_sources(VersebPreview PRIVATE Tests/Preview.cpp)
    target_include_directories(VersebPreview PRIVATE Source $<TARGET_PROPERTY:Verseb,INCLUDE_DIRECTORIES>)
    target_compile_definitions(VersebPreview PRIVATE
        "$<FILTER:$<TARGET_PROPERTY:Verseb,COMPILE_DEFINITIONS>,EXCLUDE,^(JUCE_STANDALONE_APPLICATION|JucePlugin_Build_Standalone)=>")
    target_link_libraries(VersebPreview PRIVATE Verseb)
endif()
if(APPLE)
    # Local ad-hoc signature after JUCE has written resources and the VST3 manifest.
    # It does not publish, notarise or install the bundles.
    add_custom_target(VersebAdHocSign ALL)
    set(sign_targets Verseb_VST3 Verseb_AU Verseb_Standalone)
    if(TARGET VersebPreview)
        list(APPEND sign_targets VersebPreview)
    endif()
    foreach(sign_target IN LISTS sign_targets)
        add_dependencies(VersebAdHocSign ${sign_target})
        add_custom_command(TARGET VersebAdHocSign POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:${sign_target}>"
            COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:${sign_target}>"
            VERBATIM)
    endforeach()
endif()
