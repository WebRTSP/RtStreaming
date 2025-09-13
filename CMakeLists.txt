cmake_minimum_required(VERSION 3.10)

project(RtStreaming)

option(ONVIF_SUPPORT "ONVIF support" ON)

add_subdirectory(deps/CxxPtr)
add_subdirectory(GstRtStreaming)
if(ONVIF_SUPPORT)
    add_subdirectory(deps/ONVIF)
endif()

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.cpp
    *.h
    *.cmake)

add_library(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME}
    GstRtStreaming)
set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
