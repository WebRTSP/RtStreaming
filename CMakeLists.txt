cmake_minimum_required(VERSION 3.0)

project(RtStreaming)

add_subdirectory(deps/CxxPtr)
add_subdirectory(deps/ONVIF)
add_subdirectory(GstRtStreaming)

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.cpp
    *.h
    *.cmake)

add_library(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME}
    GstRtStreaming)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
