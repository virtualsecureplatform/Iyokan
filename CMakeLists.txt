cmake_minimum_required(VERSION 3.18)

option(IYOKAN_80BIT_SECURITY "Use 80-bit security parameter for TFHE and cuFHE" OFF)

set(CMAKE_MODULE_PATH
    ${CMAKE_SOURCE_DIR}
    "${CMAKE_SOURCE_DIR}/thirdparty/sanitizers-cmake/cmake"
    ${CMAKE_MODULE_PATH}
)
set(Backward_DIR "${CMAKE_SOURCE_DIR}/thirdparty/backward-cpp")

# Defaults to CMAKE_BUILD_TYPE=Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(iyokan LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Sanitizers)
find_package(Backward)

if (IYOKAN_80BIT_SECURITY)
    set(USE_80BIT_SECURITY ON CACHE BOOL "")
endif()
if (IYOKAN_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    # https://gitlab.kitware.com/cmake/cmake/-/issues/23079
    if(NOT DEFINED CMAKE_CUDA20_STANDARD_COMPILE_OPTION)
        set(CMAKE_CUDA20_STANDARD_COMPILE_OPTION "")
        set(CMAKE_CUDA20_EXTENSION_COMPILE_OPTION "")
    endif()
    add_subdirectory(thirdparty/cuFHE)
else()
    add_subdirectory(thirdparty/cuFHE/thirdparties/TFHEpp)
endif(IYOKAN_ENABLE_CUDA)
add_subdirectory(thirdparty/spdlog)

set(IYOKAN_CXXFLAGS -Wall -Wextra -Wno-sign-compare)
set(IYOKAN_CXXFLAGS_DEBUG   ${IYOKAN_CXXFLAGS} -O0 -g3)
set(IYOKAN_CXXFLAGS_RELEASE ${IYOKAN_CXXFLAGS} -O3 -march=native -DNDEBUG -g3)
set(IYOKAN_INCLUDE_DIRS
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/picojson>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ThreadPool>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/CLI11/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cereal/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ELFIO>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/toml11>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/spdlog/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fmt/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cuFHE/include>
)
if (IYOKAN_ENABLE_CUDA)
    list(APPEND IYOKAN_INCLUDE_DIRS ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
endif(IYOKAN_ENABLE_CUDA)
set(IYOKAN_LIBS tfhe++ Threads::Threads OpenMP::OpenMP_CXX Backward::Backward stdc++fs)
if (IYOKAN_80BIT_SECURITY)
    # For TFHEpp headers
    list(APPEND IYOKAN_COMPILE_DEFINITIONS USE_80BIT_SECURITY)
endif()

set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_RELEASE "")

# Get git revision
# Thanks to: https://stackoverflow.com/a/6526533
set(IYOKAN_GIT_REVISION "unknown")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
    find_package(Git)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE "IYOKAN_GIT_REVISION"
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        message(STATUS "Git revision: ${IYOKAN_GIT_REVISION}")
    endif(GIT_FOUND)
endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
list(APPEND IYOKAN_COMPILE_DEFINITIONS IYOKAN_GIT_REVISION="${IYOKAN_GIT_REVISION}")

add_subdirectory(src)
