cmake_minimum_required(VERSION 3.22)

project(Merge VERSION 0.9.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(MERGE_BUILD_APP   "Build the Merge desktop application" ON)
option(MERGE_BUILD_TESTS "Build the MergeTests unit test runner" ON)
option(MERGE_GENERATE_SAMPLES "Generate the factory sample library at build time (needs Python 3)" ON)
option(MERGE_WITH_ONNX   "Enable HQ stem separation through ONNX Runtime" OFF)
set(MERGE_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (enables ASIO on Windows)")
set(MERGE_ONNXRUNTIME_DIR "" CACHE PATH "Path to an extracted ONNX Runtime release (include/ + lib/)")

if(MSVC)
    add_compile_options(/bigobj /utf-8 /MP)
    # self-contained Merge.exe: no Visual C++ redistributable needed
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)

# Local overrides (handy offline): -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE etc.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.3
    GIT_SHALLOW ON)
FetchContent_Declare(signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 1.4.0
    GIT_SHALLOW ON
    SOURCE_SUBDIR do-not-add)
FetchContent_Declare(signalsmith_linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG 0.6.4
    GIT_SHALLOW ON
    SOURCE_SUBDIR do-not-add)
FetchContent_Declare(sqlite
    URL https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    SOURCE_SUBDIR do-not-add)

FetchContent_MakeAvailable(JUCE signalsmith_stretch signalsmith_linear sqlite)

# --- third-party helper targets ------------------------------------------------
add_library(merge_stretch INTERFACE)
target_include_directories(merge_stretch SYSTEM INTERFACE
    ${signalsmith_stretch_SOURCE_DIR}
    ${signalsmith_stretch_SOURCE_DIR}/include
    ${signalsmith_linear_SOURCE_DIR}/include)

add_library(merge_sqlite STATIC ${sqlite_SOURCE_DIR}/sqlite3.c)
target_include_directories(merge_sqlite SYSTEM PUBLIC ${sqlite_SOURCE_DIR})
target_compile_definitions(merge_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION=1 SQLITE_DQS=0)
set_target_properties(merge_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(merge_sqlite PRIVATE -w)
endif()
find_package(Threads REQUIRED)
target_link_libraries(merge_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})

# --- factory samples -------------------------------------------------------------
set(MERGE_SAMPLES_DIR ${CMAKE_BINARY_DIR}/Samples)
if(MERGE_GENERATE_SAMPLES)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        set(_stamp ${MERGE_SAMPLES_DIR}/Factory/.stamp)
        add_custom_command(OUTPUT ${_stamp}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/samplegen/generate.py --out ${MERGE_SAMPLES_DIR}/Factory
            DEPENDS ${CMAKE_SOURCE_DIR}/tools/samplegen/generate.py
            COMMENT "Generating Merge factory sample library"
            VERBATIM)
        add_custom_target(merge_samples ALL DEPENDS ${_stamp})
    else()
        message(WARNING "Python 3 not found - factory samples will not be generated")
    endif()
endif()

# Bundled packs: copy samples/user-packs/<Pack>/ next to the factory library (validated in CI).
file(GLOB _merge_user_packs LIST_DIRECTORIES true ${CMAKE_SOURCE_DIR}/samples/user-packs/*)
foreach(_pack ${_merge_user_packs})
    if(IS_DIRECTORY ${_pack} AND EXISTS ${_pack}/pack.json)
        file(COPY ${_pack} DESTINATION ${MERGE_SAMPLES_DIR}/Packs)
    endif()
endforeach()

# --- sources ---------------------------------------------------------------------------
file(GLOB_RECURSE MERGE_CORE_SOURCES CONFIGURE_DEPENDS
    src/core/*.cpp src/engine/*.cpp src/dj/*.cpp src/analysis/*.cpp
    src/stems/*.cpp src/library/*.cpp src/controllers/*.cpp)
file(GLOB_RECURSE MERGE_CORE_HEADERS CONFIGURE_DEPENDS
    src/core/*.h src/engine/*.h src/dj/*.h src/analysis/*.h
    src/stems/*.h src/library/*.h src/controllers/*.h)
file(GLOB_RECURSE MERGE_UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp src/app/*.cpp)
file(GLOB_RECURSE MERGE_UI_HEADERS CONFIGURE_DEPENDS src/ui/*.h src/app/*.h)

function(merge_configure target)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1
        JUCE_USE_WINDOWS_MEDIA_FORMAT=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        MERGE_VERSION_STRING="${PROJECT_VERSION}"
        MERGE_DEV_SAMPLES_DIR="${MERGE_SAMPLES_DIR}"
        JUCE_APPLICATION_NAME_STRING="Merge"
        JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
    if(WIN32 AND MERGE_ASIO_SDK_DIR)
        target_compile_definitions(${target} PRIVATE JUCE_ASIO=1)
        target_include_directories(${target} PRIVATE ${MERGE_ASIO_SDK_DIR}/common)
    endif()
    if(MERGE_WITH_ONNX AND MERGE_ONNXRUNTIME_DIR)
        target_compile_definitions(${target} PRIVATE MERGE_WITH_ONNX=1)
        target_include_directories(${target} PRIVATE ${MERGE_ONNXRUNTIME_DIR}/include)
        if(WIN32)
            target_link_libraries(${target} PRIVATE ${MERGE_ONNXRUNTIME_DIR}/lib/onnxruntime.lib)
        elseif(APPLE)
            target_link_libraries(${target} PRIVATE ${MERGE_ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib)
        else()
            target_link_libraries(${target} PRIVATE ${MERGE_ONNXRUNTIME_DIR}/lib/libonnxruntime.so)
        endif()
    endif()
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
            merge_stretch
            merge_sqlite
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endfunction()

# --- embedded resources (fonts) -----------------------------------------------------------
juce_add_binary_data(merge_resources
    HEADER_NAME MergeResources.h
    NAMESPACE MergeResources
    SOURCES
        resources/fonts/Inter-Regular.otf
        resources/fonts/Inter-SemiBold.otf)
set_target_properties(merge_resources PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- application -------------------------------------------------------------------------
if(MERGE_BUILD_APP)
    juce_add_gui_app(Merge
        PRODUCT_NAME "Merge"
        COMPANY_NAME "Merge"
        BUNDLE_ID "org.merge.app"
        ICON_BIG ${CMAKE_SOURCE_DIR}/resources/merge-icon.png
        DOCUMENT_EXTENSIONS merge
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Merge records audio from your interface when you sample or record.")
    target_sources(Merge PRIVATE ${MERGE_CORE_SOURCES} ${MERGE_CORE_HEADERS} ${MERGE_UI_SOURCES} ${MERGE_UI_HEADERS})
    merge_configure(Merge)
    target_link_libraries(Merge PRIVATE juce::juce_gui_extra merge_resources)
    if(TARGET merge_samples)
        add_dependencies(Merge merge_samples)
    endif()
    source_group(TREE ${CMAKE_SOURCE_DIR}/src FILES ${MERGE_CORE_SOURCES} ${MERGE_CORE_HEADERS} ${MERGE_UI_SOURCES} ${MERGE_UI_HEADERS})
endif()

# --- tests ------------------------------------------------------------------------------------
if(MERGE_BUILD_TESTS)
    enable_testing()
    file(GLOB MERGE_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.h)
    juce_add_console_app(MergeTests PRODUCT_NAME "MergeTests")
    target_sources(MergeTests PRIVATE ${MERGE_CORE_SOURCES} ${MERGE_CORE_HEADERS} ${MERGE_TEST_SOURCES})
    merge_configure(MergeTests)
    if(TARGET merge_samples)
        add_dependencies(MergeTests merge_samples)
    endif()
    add_test(NAME MergeTests COMMAND MergeTests)
    set_tests_properties(MergeTests PROPERTIES ENVIRONMENT "MERGE_SAMPLES_DIR=${MERGE_SAMPLES_DIR}" TIMEOUT 900
                         PASS_REGULAR_EXPRESSION "ALL TESTS PASSED")
endif()
