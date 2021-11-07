add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-8-8.o
                   COMMAND ${CMAKE_OBJCOPY}
                   ARGS -I binary -O elf64-x86-64 -B i386 mux-ram-8-8-8.min.json ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-8-8.o
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-16-16.o
                   COMMAND ${CMAKE_OBJCOPY}
                   ARGS -I binary -O elf64-x86-64 -B i386 mux-ram-8-16-16.min.json ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-16-16.o
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-9-16-16.o
                   COMMAND ${CMAKE_OBJCOPY}
                   ARGS -I binary -O elf64-x86-64 -B i386 mux-ram-9-16-16.min.json ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-9-16-16.o
                   WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

##### iyokan
add_executable(iyokan
    main.cpp iyokan.cpp iyokan_plain.cpp iyokan_tfhepp.cpp error.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-8-8.o
    ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-16-16.o
    ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-9-16-16.o)
add_sanitizers(iyokan)
target_compile_options(iyokan PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(iyokan PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(iyokan ${IYOKAN_LIBS})
target_include_directories(iyokan PRIVATE ${IYOKAN_INCLUDE_DIRS})
target_compile_definitions(iyokan PRIVATE ${IYOKAN_COMPILE_DEFINITIONS})
if (IYOKAN_ENABLE_CUDA)
    target_sources(iyokan PRIVATE iyokan_cufhe.cpp)
    target_link_libraries(iyokan cufhe_gpu ${CUDA_LIBRARIES})
    target_compile_definitions(iyokan PRIVATE IYOKAN_CUDA_ENABLED)
endif(IYOKAN_ENABLE_CUDA)

##### test0
add_executable(test0
    test0.cpp
    iyokan_nt.cpp iyokan_nt_plain.cpp error_nt.cpp packet_nt.cpp dataholder_nt.cpp
    network_reader.cpp blueprint.cpp label.cpp allocator.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-8-8.o
    ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-8-16-16.o
    ${CMAKE_CURRENT_BINARY_DIR}/mux-ram-9-16-16.o)
add_sanitizers(test0)
target_compile_options(test0 PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(test0 PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(test0 ${IYOKAN_LIBS})
target_include_directories(test0 PRIVATE ${IYOKAN_INCLUDE_DIRS})
target_compile_definitions(test0 PRIVATE ${IYOKAN_COMPILE_DEFINITIONS})
#if (IYOKAN_ENABLE_CUDA)
#    target_sources(test0 PRIVATE iyokan_cufhe.cpp)
#    target_link_libraries(test0 cufhe_gpu ${CUDA_LIBRARIES})
#    target_compile_definitions(test0 PRIVATE IYOKAN_CUDA_ENABLED)
#endif(IYOKAN_ENABLE_CUDA)

##### iyokan-packet
add_executable(iyokan-packet iyokan-packet.cpp error.cpp)
add_sanitizers(iyokan-packet)
target_compile_options(iyokan-packet PUBLIC "$<$<CONFIG:DEBUG>:${IYOKAN_CXXFLAGS_DEBUG}>")
target_compile_options(iyokan-packet PUBLIC "$<$<CONFIG:RELEASE>:${IYOKAN_CXXFLAGS_RELEASE}>")
target_link_libraries(iyokan-packet ${IYOKAN_LIBS})
target_include_directories(iyokan-packet PRIVATE ${IYOKAN_INCLUDE_DIRS})
target_compile_definitions(iyokan-packet PRIVATE ${IYOKAN_COMPILE_DEFINITIONS})

set_target_properties(iyokan test0 iyokan-packet
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
install(TARGETS iyokan iyokan-packet DESTINATION bin)
