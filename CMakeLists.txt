cmake_minimum_required (VERSION 3.10)

project (AirlineTycoon)

set(CMAKE_CXX_STANDARD 17)

# Build name set by the workflow (e.g. preview-dev-031026); shown in game, logged and part of the net handshake
if(AT_BUILD_LABEL)
    add_compile_definitions(AT_BUILD_LABEL="${AT_BUILD_LABEL}")
endif()

# HD-Rendering (Phase 2) braucht mehr als 4 GB Adressraum: nur 64-Bit-Builds
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Airline Tycoon muss als 64-Bit-Programm gebaut werden (z. B. cmake -A x64).")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
elseif (CMAKE_BUILD_TYPE STREQUAL "Tidy")
    set(CMAKE_BUILD_TYPE Release)
    add_compile_definitions(NDEBUG)
    # enable clang-tidy
    set(CMAKE_CXX_CLANG_TIDY clang-tidy; -header-filter=.;)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")
set(GAMEFILES_FOLDER "${PROJECT_SOURCE_DIR}/gamefiles")
set(PATCH_FOLDER "${PROJECT_SOURCE_DIR}/patch")

if (MSVC)
    add_compile_options("/Zi")

    add_link_options("/DEBUG:FULL")
    add_link_options("/OPT:REF")
    add_link_options("/OPT:ICF")
endif()

if (WIN32)
    # Set and bind SDL folders
    set(SDL2_PATH "${CMAKE_MODULE_PATH}/sdl2")
    set(SDL2_MIXER_PATH "${CMAKE_MODULE_PATH}/sdl2-mixer")
    set(SDL2_IMAGE_PATH "${CMAKE_MODULE_PATH}/sdl2-image")
    set(SDL2_TTF_PATH "${CMAKE_MODULE_PATH}/sdl2-ttf")

    # Download when SDL is not found
    if(NOT EXISTS ${SDL2_TTF_PATH})
        message(STATUS "Downloading SDL2. This is a (longer) one time operation. Please wait.")
        execute_process(
            COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_MODULE_PATH}/dl-sdl2.ps1
            WORKING_DIRECTORY ${CMAKE_MODULE_PATH}
        )
    endif()

    # Copy SDL dependencies
    set(DEPENDENCIES_PATH ${DEPENDENCIES_PATH} "${CMAKE_BINARY_DIR}/dependencies")
    file(MAKE_DIRECTORY ${DEPENDENCIES_PATH})
    file(GLOB_RECURSE dll_files ${SDL2_PATH}/*.dll ${SDL2_MIXER_PATH}/*.dll ${SDL2_IMAGE_PATH}/*.dll ${SDL2_TTF_PATH}/*.dll ${PROJECT_SOURCE_DIR}/libs/*.dll ${PROJECT_SOURCE_DIR}/libs/*.exe)
    list(FILTER dll_files INCLUDE REGEX "x64|\\.exe$")
    foreach(file ${dll_files})
        file(COPY ${file} DESTINATION ${DEPENDENCIES_PATH})
    endforeach()
endif()

if (WIN32 OR APPLE)
    # Needed on windows as include is not under repo / project
    include_directories(libs/jansson/include)
endif()

include_directories(libs/libflic/repo)

find_package(SDL2main REQUIRED)
find_package(SDL2_mixer REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)

# Include external libs.
add_subdirectory (libs)

# Include sub-projects.
add_subdirectory (src)

