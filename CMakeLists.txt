cmake_minimum_required(VERSION 3.8)

project(ffmpeg-record)

set(CMAKE_CXX_FLAGS "-Wno-error=deprecated-declarations -Wno-deprecated-declarations ")
include_directories("D:/src/vcpkg/installed/x64-windows-static/include/")
link_directories("D:/src/vcpkg/installed/x64-windows-static/lib/")

find_package(SDL2 CONFIG REQUIRED)
add_definitions(-D_WINDLL)

add_library(videotools "VideoPlay.h" "VideoPlay.cpp")


target_link_libraries(videotools PRIVATE avcodec.lib avdevice.lib avfilter.lib avutil.lib avformat.lib postproc.lib SDL2.lib swresample.lib swscale.lib)
target_link_libraries(videotools PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2::SDL2-static)
