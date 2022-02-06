cmake_minimum_required(VERSION 3.8)

project(ffmpeg-record)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
set(CMAKE_CXX_FLAGS "-Wno-error=deprecated-declarations -Wno-deprecated-declarations ")
endif()
if(MSVC)
include_directories("D:/src/vcpkg/installed/x64-windows-static/include/")
link_directories("D:/src/vcpkg/installed/x64-windows-static/lib/")
include_directories("D:/boost_1_78_0")
link_directories("D:/boost_1_78_0/stage/lib")
endif()

find_package(SDL2 CONFIG REQUIRED)
add_definitions(-D_WINDLL)

add_library(videotools "VideoPlay.h" "VideoPlay.cpp")

if(MSVC)
set(additional_dependencies
dxva2.lib
evr.lib
mf.lib
mfplat.lib
mfplay.lib
mfreadwrite.lib
mfuuid.lib
bcrypt.lib
ws2_32.lib
Secur32.lib
Strmiids.lib)
endif()

target_link_libraries(videotools PRIVATE avcodec.lib avdevice.lib avfilter.lib avutil.lib avformat.lib SDL2.lib swresample.lib swscale.lib)
if(MSVC)
target_link_libraries(videotools PRIVATE ${additional_dependencies})
endif()
target_link_libraries(videotools PRIVATE SDL2::SDL2 SDL2::SDL2main SDL2::SDL2-static)


add_executable(test-client "test-client.cpp" "VideoPlay.h")
target_link_libraries(test-client PRIVATE videotools)
#  bufferoverflowU.lib