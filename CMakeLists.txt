cmake_minimum_required(VERSION 3.8)

project(ffmpeg-record)

set(CMAKE_CXX_FLAGS "-Wno-error=deprecated-declarations -Wno-deprecated-declarations ")
# include_directories("D:/src/vcpkg/installed/x64-windows-static/include/")
# link_directories("D:/src/vcpkg/installed/x64-windows-static/lib/")

find_package(SDL2 CONFIG REQUIRED)
add_definitions(-D_WINDLL)

add_library(videotools "VideoPlay.h" "VideoPlay.cpp")

# set(additional_dependencies
# dxva2.lib
# evr.lib
# mf.lib
# mfplat.lib
# mfplay.lib
# mfreadwrite.lib
# mfuuid.lib
# bcrypt.lib
# ws2_32.lib
# Secur32.lib
# Strmiids.lib)

target_link_libraries(videotools PRIVATE avcodec.lib avdevice.lib avfilter.lib avutil.lib avformat.lib postproc.lib SDL2.lib swresample.lib swscale.lib)
# target_link_libraries(videotools PRIVATE ${additional_dependencies})
target_link_libraries(videotools PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2::SDL2-static)


add_executable(test-client "test-client.cpp" "VideoPlay.h")
target_link_libraries(test-client PRIVATE videotools)
#  bufferoverflowU.lib