cmake_minimum_required(VERSION 3.5)

project(GstRtStreaming)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(GSTREAMER REQUIRED gstreamer-1.0)
pkg_search_module(GSTREAMER_SDP REQUIRED gstreamer-sdp-1.0)
pkg_search_module(GSTREAMER_WEBRTC REQUIRED gstreamer-webrtc-1.0)
pkg_search_module(GSTREAMER_PBUTILS REQUIRED gstreamer-pbutils-1.0)
pkg_search_module(SPDLOG REQUIRED spdlog)
pkg_search_module(NICE REQUIRED nice)

file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.c
    *.cpp
    *.h
    *.cmake)

if(ONVIF_SUPPORT)
    file(GLOB ONVIF_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
        ONVIF/*.cpp
        ONVIF/*.h)
endif()

add_library(${PROJECT_NAME} ${SOURCES} ${ONVIF_SOURCES})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_compile_definitions(${PROJECT_NAME} PUBLIC -DGST_USE_UNSTABLE_API)
target_include_directories(${PROJECT_NAME} PUBLIC
    ${GLIB_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_SDP_INCLUDE_DIRS}
    ${GSTREAMER_WEBRTC_INCLUDE_DIRS}
    ${GSTREAMER_PBUTILS_INCLUDE_DIRS}
    ${WS_INCLUDE_DIRS}
    ${SPDLOG_INCLUDE_DIRS}
    ${NICE_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PUBLIC ${SPDLOG_CFLAGS})
target_link_libraries(${PROJECT_NAME}
    ${GLIB_LDFLAGS}
    ${GSTREAMER_LINK_LIBRARIES}
    ${GSTREAMER_SDP_LINK_LIBRARIES}
    ${GSTREAMER_WEBRTC_LINK_LIBRARIES}
    ${GSTREAMER_PBUTILS_LINK_LIBRARIES}
    ${SPDLOG_LDFLAGS}
    ${NICE_LDFLAGS}
    CxxPtr)

if(ONVIF_SUPPORT)
    target_link_libraries(${PROJECT_NAME} ONVIF)
endif()
