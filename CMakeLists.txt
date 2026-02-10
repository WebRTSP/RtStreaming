cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)

project(RtStreaming)

if(ANDROID)
    set(ONVIF_SUPPORT_DEFAULT OFF)
else()
    set(ONVIF_SUPPORT_DEFAULT ON)
endif()

option(ONVIF_SUPPORT "ONVIF support" ${ONVIF_SUPPORT_DEFAULT})

if(ANDROID AND NOT DEFINED GSTREAMER_ANDROID_ROOT)
    if(NOT DEFINED ENV{GSTREAMER_ANDROID_ROOT})
       message(FATAL_ERROR "GSTREAMER_ANDROID_ROOT environment variable is not defined")
    endif()

    set(GSTREAMER_ANDROID_ROOT $ENV{GSTREAMER_ANDROID_ROOT})

    if(${CMAKE_ANDROID_ARCH_ABI} STREQUAL "x86" OR ${CMAKE_ANDROID_ARCH_ABI} STREQUAL "x86_64")
        set(GSTREAMER_ANDROID_ARCH ${CMAKE_ANDROID_ARCH_ABI})
    elseif(${CMAKE_ANDROID_ARCH_ABI} STREQUAL "arm64-v8a")
        set(GSTREAMER_ANDROID_ARCH arm64)
    elseif(${CMAKE_ANDROID_ARCH_ABI} STREQUAL "armeabi-v7a")
        set(GSTREAMER_ANDROID_ARCH armv7)
    endif()

    set(GSTREAMER_ANDROID_ROOT ${GSTREAMER_ANDROID_ROOT}/${GSTREAMER_ANDROID_ARCH})

    set(GSTREAMER_PKG_PATHS
        "${GSTREAMER_ANDROID_ROOT}/lib/pkgconfig"
        "${GSTREAMER_ANDROID_ROOT}/lib/gstreamer-1.0/pkgconfig"
    )

    cmake_path(CONVERT "${GSTREAMER_PKG_PATHS}" TO_NATIVE_PATH_LIST GSTREAMER_PKG_PATHS)

    set(ENV{PKG_CONFIG_PATH} "${GSTREAMER_PKG_PATHS}")
endif()

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
