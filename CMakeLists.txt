cmake_minimum_required(VERSION 3.5)

# Defaults to CMAKE_BUILD_TYPE=Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(iyokan)

find_package(Threads REQUIRED)
find_package(OpenMP REQUIRED)

add_subdirectory(TFHEpp)

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

##### iyokan
add_executable(iyokan iyokan.cpp)
target_compile_options(iyokan PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(iyokan PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(iyokan ${IYOKAN_LIBS})
target_include_directories(iyokan PRIVATE ${IYOKAN_INCLUDE_DIRS})

##### test0
add_executable(test0 test0.cpp)
target_compile_options(test0 PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(test0 PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(test0 ${IYOKAN_LIBS})
target_include_directories(test0 PRIVATE ${IYOKAN_INCLUDE_DIRS})

##### kvsp-packet
add_executable(kvsp-packet kvsp-packet.cpp)
target_compile_options(kvsp-packet PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(kvsp-packet PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(kvsp-packet ${IYOKAN_LIBS})
target_include_directories(kvsp-packet PRIVATE ${IYOKAN_INCLUDE_DIRS})

set_target_properties(iyokan test0 kvsp-packet
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
