if (Test-Path -Path build) {
    Remove-Item -Path build -Recurse
}
ms22
cmake -B build -G "Ninja" -DCMAKE_TOOLCHAIN_FILE="D:/src/vcpkg/scripts/buildsystems/vcpkg.cmake" `
-DSDL2_DIR="D:/src/vcpkg/installed/x64-windows-static/share/sdl2/"
cmake --build build
