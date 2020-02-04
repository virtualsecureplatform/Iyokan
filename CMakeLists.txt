cmake_minimum_required(VERSION 3.5)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/sanitizers-cmake/cmake" ${CMAKE_MODULE_PATH})

# Defaults to CMAKE_BUILD_TYPE=Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(iyokan LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)
find_package(Sanitizers)

add_subdirectory(TFHEpp)
if (IYOKAN_ENABLE_CUDA)
    enable_language(CUDA)
    add_subdirectory(cuFHE/cufhe)
endif(IYOKAN_ENABLE_CUDA)

set(IYOKAN_CXXFLAGS -Wall -Wextra -pedantic -g3)
set(IYOKAN_CXXFLAGS_DEBUG   ${IYOKAN_CXXFLAGS} -O0)
set(IYOKAN_CXXFLAGS_RELEASE ${IYOKAN_CXXFLAGS} -O3 -march=native)
set(IYOKAN_INCLUDE_DIRS
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/picojson>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ThreadPool>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/CLI11/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cereal/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ELFIO>
)
set(IYOKAN_LIBS tfhe++ Threads::Threads OpenMP::OpenMP_CXX)

set(CMAKE_C_FLAGS "")
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS "")
set(CMAKE_CXX_FLAGS_DEBUG "")
set(CMAKE_CXX_FLAGS_RELEASE "")

##### iyokan
add_executable(iyokan iyokan.cpp iyokan_plain.cpp iyokan_tfhepp.cpp)
add_sanitizers(iyokan)
target_compile_options(iyokan PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(iyokan PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(iyokan ${IYOKAN_LIBS})
target_include_directories(iyokan PRIVATE ${IYOKAN_INCLUDE_DIRS})
if (IYOKAN_ENABLE_CUDA)
    target_sources(iyokan PRIVATE iyokan_cufhe.cpp)
    target_link_libraries(iyokan cufhe_gpu ${CUDA_LIBRARIES})
    target_compile_definitions(iyokan PRIVATE IYOKAN_CUDA_ENABLED)
endif(IYOKAN_ENABLE_CUDA)

##### test0
add_executable(test0 test0.cpp iyokan_plain.cpp iyokan_tfhepp.cpp)
add_sanitizers(test0)
target_compile_options(test0 PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(test0 PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(test0 ${IYOKAN_LIBS})
target_include_directories(test0 PRIVATE ${IYOKAN_INCLUDE_DIRS})
if (IYOKAN_ENABLE_CUDA)
    target_sources(test0 PRIVATE iyokan_cufhe.cpp)
    target_link_libraries(test0 cufhe_gpu ${CUDA_LIBRARIES})
    target_compile_definitions(test0 PRIVATE IYOKAN_CUDA_ENABLED)
endif(IYOKAN_ENABLE_CUDA)

##### kvsp-packet
add_executable(kvsp-packet kvsp-packet.cpp)
add_sanitizers(kvsp-packet)
target_compile_options(kvsp-packet PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(kvsp-packet PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(kvsp-packet ${IYOKAN_LIBS})
target_include_directories(kvsp-packet PRIVATE ${IYOKAN_INCLUDE_DIRS})

set_target_properties(iyokan test0 kvsp-packet
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
