cmake_minimum_required(VERSION 3.10)

project(GstRtStreaming)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GLIB
    REQUIRED
        glib-2.0
)
pkg_check_modules(GSTREAMER
    REQUIRED
        gstreamer-1.0
        gstreamer-sdp-1.0
        gstreamer-webrtc-1.0
        gstreamer-app-1.0
        gstreamer-pbutils-1.0
        nice
 )

if(ANDROID)
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.17.0
    )
    FetchContent_MakeAvailable(spdlog)

    pkg_check_modules(GSTREAMER_PLUGINS
        REQUIRED
            gstdebug
            gstcoreelements
            gstvideoconvertscale
            gstnice
            gstrtp
            gstsrtp
            gstdtls
            gstrtpmanager
            gstwebrtc
            gstlibav
    )
else()
    pkg_search_module(SPDLOG REQUIRED spdlog)
endif()

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
target_compile_definitions(${PROJECT_NAME} PUBLIC -DGST_USE_UNSTABLE_API)
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${GLIB_INCLUDE_DIRS}
)
target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${GSTREAMER_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME}
    CxxPtr
)

if(ANDROID)
    target_link_libraries(${PROJECT_NAME}
        ${GLIB_STATIC_LDFLAGS}
        ${GSTREAMER_STATIC_LDFLAGS}
        ${GSTREAMER_PLUGINS_STATIC_LDFLAGS}
        spdlog::spdlog
    )
else()
    target_include_directories(${PROJECT_NAME}
        PUBLIC
            ${SPDLOG_INCLUDE_DIRS}
    )
    target_link_libraries(${PROJECT_NAME}
        ${GLIB_LDFLAGS}
        ${GSTREAMER_LDFLAGS}
        ${SPDLOG_LDFLAGS}
    )
endif()

if(ONVIF_SUPPORT)
    target_link_libraries(${PROJECT_NAME} ONVIF)
endif()
