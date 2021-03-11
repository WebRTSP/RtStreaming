cmake_minimum_required(VERSION 3.0)

project(RtcStreaming)

add_subdirectory(deps/CxxPtr)
add_subdirectory(GstRtcStreaming)

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.cpp
    *.h
    *.cmake)

add_library(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME}
    CxxPtr
    GstRtcStreaming)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
