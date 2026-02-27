cmake_minimum_required(VERSION 3.18)

option(IYOKAN_80BIT_SECURITY "Use 80-bit security parameter for TFHE and cuFHE" OFF)
# IYOKAN_MARCH: -march target passed to TFHEpp and Iyokan itself.
# Use "x86-64-v3" for a portable AVX2 build, "native" (default) for best
# performance on the build host.
set(IYOKAN_MARCH "native" CACHE STRING "Target -march flag for TFHEpp and Iyokan")

set(CMAKE_MODULE_PATH
    ${CMAKE_SOURCE_DIR}
    "${CMAKE_SOURCE_DIR}/thirdparty/sanitizers-cmake/cmake"
    ${CMAKE_MODULE_PATH}
)

# Defaults to CMAKE_BUILD_TYPE=Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(iyokan LANGUAGES CXX C)

add_subdirectory(thirdparty/backward-cpp)

find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Sanitizers)

if (IYOKAN_80BIT_SECURITY)
    set(USE_80BIT_SECURITY ON CACHE BOOL "")
endif()
# Forward IYOKAN_MARCH to TFHEpp as TFHEPP_MARCH (normal variable, inherited by
# subdirectories including cuFHEpp/thirdparties/TFHEpp via add_subdirectory).
set(TFHEPP_MARCH "${IYOKAN_MARCH}")
if (IYOKAN_ENABLE_CUDA)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 20)
    # https://gitlab.kitware.com/cmake/cmake/-/issues/23079
    if(NOT DEFINED CMAKE_CUDA20_STANDARD_COMPILE_OPTION)
        set(CMAKE_CUDA20_STANDARD_COMPILE_OPTION "")
        set(CMAKE_CUDA20_EXTENSION_COMPILE_OPTION "")
    endif()
    # Disable key-bundle bootstrapping: cuFHEpp's BootstrapTLWE2TRLWE (used for
    # CMUX RAM gate bootstrapping) always uses __BlindRotate__ which expects
    # regular BSK format, but USE_KEY_BUNDLE stores the BSK in key-bundle format,
    # causing wrong RAM results. Disable key bundles to keep BSK in regular format.
    set(USE_KEY_BUNDLE OFF CACHE BOOL "" FORCE)
    add_subdirectory(thirdparty/cuFHEpp)
else()
    add_subdirectory(thirdparty/cuFHEpp/thirdparties/TFHEpp)
endif(IYOKAN_ENABLE_CUDA)

# Workaround: GCC < 14 does not support std::make_signed<__int128>.
# TFHEpp's lvl3param uses __uint128_t, causing compilation failure.
target_compile_options(tfhe++ PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/src/int128_make_signed_fix.hpp)

add_subdirectory(thirdparty/spdlog)

set(IYOKAN_CXXFLAGS -Wall -Wextra -Wno-sign-compare)
set(IYOKAN_CXXFLAGS_DEBUG   ${IYOKAN_CXXFLAGS} -O0 -g3)
set(IYOKAN_CXXFLAGS_RELEASE ${IYOKAN_CXXFLAGS} -O3 -march=${IYOKAN_MARCH} -DNDEBUG -g3)
set(IYOKAN_INCLUDE_DIRS
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/picojson>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ThreadPool>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/CLI11/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cuFHEpp/thirdparties/TFHEpp/thirdparties/cereal/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ELFIO>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/toml11/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/spdlog/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fmt/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cuFHEpp/include>
)
if (IYOKAN_ENABLE_CUDA)
    list(APPEND IYOKAN_INCLUDE_DIRS ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
    # Note: USE_KEY_BUNDLE intentionally not propagated here (disabled above).
endif(IYOKAN_ENABLE_CUDA)
set(IYOKAN_LIBS tfhe++ Threads::Threads OpenMP::OpenMP_CXX Backward::Backward stdc++fs)
if (IYOKAN_80BIT_SECURITY)
    # For TFHEpp headers
    list(APPEND IYOKAN_COMPILE_DEFINITIONS USE_80BIT_SECURITY)
endif()
# Propagate TFHEpp compile definitions to Iyokan targets so that header-only
# template functions (CircuitBootstrapping, ExternalProduct, etc.) see the same
# preprocessor state as the tfhe++ library, avoiding ODR violations.
if(USE_BLAKE3)
    list(APPEND IYOKAN_COMPILE_DEFINITIONS USE_BLAKE3)
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
