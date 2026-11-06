cmake_minimum_required(VERSION 3.22)

project(Pluginz500Suite VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Reuse existing local JUCE source if already fetched in Neve1073Preamp/build/_deps/juce-src
set(LOCAL_CACHED_JUCE "${CMAKE_CURRENT_SOURCE_DIR}/Neve1073Preamp/build/_deps/juce-src")

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
elseif(EXISTS "${LOCAL_CACHED_JUCE}/CMakeLists.txt")
    message(STATUS "Reusing local JUCE checkout at ${LOCAL_CACHED_JUCE}")
    add_subdirectory("${LOCAL_CACHED_JUCE}" JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# Build all 500-series modules + the LBX-500 6-slot Lunchbox container plugin
add_subdirectory(Neve1073Preamp)
add_subdirectory(Neve1073EQ)
add_subdirectory(Chassis500)
