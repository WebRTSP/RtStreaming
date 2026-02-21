cmake_minimum_required(VERSION 3.10)

project(GstRtStreaming)

if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
endif()

if(ANDROID OR WIN32)
    include(FetchContent)

    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.17.0
    )
    FetchContent_MakeAvailable(spdlog)
else()
    pkg_search_module(SPDLOG REQUIRED spdlog)
endif()

if(WIN32)
    set(GSTREAMER_ROOT_DIR "$ENV{GSTREAMER_1_0_ROOT_MSVC_X86_64}")
    set(GSTREAMER_INCLUDE_DIRS
        "${GSTREAMER_ROOT_DIR}/include"
        "${GSTREAMER_ROOT_DIR}/include/glib-2.0"
        "${GSTREAMER_ROOT_DIR}/lib/glib-2.0/include"
        "${GSTREAMER_ROOT_DIR}/include/gstreamer-1.0"
    )
    set(GSTREAMER_LIBRARIES
        "${GSTREAMER_ROOT_DIR}/lib/glib-2.0.lib"
        "${GSTREAMER_ROOT_DIR}/lib/gobject-2.0.lib"
        "${GSTREAMER_ROOT_DIR}/lib/gstreamer-1.0.lib"
        "${GSTREAMER_ROOT_DIR}/lib/gstwebrtc-1.0.lib"
        "${GSTREAMER_ROOT_DIR}/lib/gstsdp-1.0.lib"
        "${GSTREAMER_ROOT_DIR}/lib/gstpbutils-1.0.lib"
        "${GSTREAMER_ROOT_DIR}/lib/nice.lib"
    )
else()
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
endif()

if(ANDROID)
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
    PUBLIC
        ${GSTREAMER_INCLUDE_DIRS}
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME}
        ${GSTREAMER_LIBRARIES}
    )
elseif(ANDROID)
    target_link_libraries(${PROJECT_NAME}
        ${GLIB_STATIC_LDFLAGS}
        ${GSTREAMER_STATIC_LDFLAGS}
        ${GSTREAMER_PLUGINS_STATIC_LDFLAGS}
    )
else()
    target_link_libraries(${PROJECT_NAME}
        ${GLIB_LDFLAGS}
        ${GSTREAMER_LDFLAGS}
    )
endif()

if(ANDROID OR WIN32)
    target_link_libraries(${PROJECT_NAME}
        spdlog::spdlog
    )
else()
    target_include_directories(${PROJECT_NAME}
        PUBLIC
            ${SPDLOG_INCLUDE_DIRS}
    )
    target_link_libraries(${PROJECT_NAME}
        ${SPDLOG_LDFLAGS}
    )
endif()

target_link_libraries(${PROJECT_NAME}
    CxxPtr
)

if(ONVIF_SUPPORT)
    target_link_libraries(${PROJECT_NAME} ONVIF)
endif()
