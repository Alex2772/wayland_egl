cmake_minimum_required(VERSION 3.16)
project(wayland_opengl)

set(CMAKE_CXX_STANDARD 20)

add_executable(wayland_opengl main.cpp)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/wayland-generated-protocols)

file(GLOB_RECURSE WAYLAND_PROTOCOLS wayland-protocols/*.xml)

foreach (WAYLAND_PROTOCOL_PATH ${WAYLAND_PROTOCOLS})
    get_filename_component(WAYLAND_PROTOCOL_NAME ${WAYLAND_PROTOCOL_PATH} NAME)
    set(OUTPUT_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/wayland-generated-protocols/${WAYLAND_PROTOCOL_NAME})
    add_custom_command(OUTPUT ${OUTPUT_NAME}.h
                       COMMAND wayland-scanner client-header ${WAYLAND_PROTOCOL_PATH} ${OUTPUT_NAME}.h)
    add_custom_command(OUTPUT ${OUTPUT_NAME}.c
            COMMAND wayland-scanner private-code ${WAYLAND_PROTOCOL_PATH} ${OUTPUT_NAME}.c)
    target_sources(wayland_opengl PRIVATE ${OUTPUT_NAME}.h ${OUTPUT_NAME}.c)
endforeach ()


target_include_directories(wayland_opengl PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include)
target_link_libraries(wayland_opengl PUBLIC wayland-client wayland-egl EGL GLESv2)

# options to tweak.
foreach(OPTION USE_FRACTIONAL_SCALE_PROTOCOL USE_VIEWPORTER_PROTOCOL USE_DECORATION_PROTOCOL USE_BLUR_PROTOCOL)
    option(${OPTION} "Enable ${OPTION}" ON)
    message(STATUS "${OPTION}=${${OPTION}}")
    target_compile_definitions(wayland_opengl PRIVATE ${OPTION}=$<IF:$<BOOL:${${OPTION}}>,1,0>)
endforeach ()

if (${USE_FRACTIONAL_SCALE_PROTOCOL} AND NOT ${USE_VIEWPORTER_PROTOCOL})
    message(FATAL_ERROR "Fractional scale protocol requires viewporter protocol")
endif ()
