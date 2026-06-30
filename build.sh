#!/bin/bash
# 异环钓鱼自动化工具 - 构建脚本
# 用法: ./build.sh [--configure]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# MSVC 环境变量
export PATH="E:/LLVM/bin:E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64:/c/Program Files/CMake/bin:$PATH"
export INCLUDE="E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/include;E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/atlmfc/include;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/winrt"
export LIB="E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/lib/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64"

if [ "$1" = "--configure" ]; then
    echo "=== 配置 CMake ==="
    cmake -B build -S . -G "Ninja" \
        -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
fi

echo "=== 编译 ==="
cmake --build build

echo "=== 完成 ==="
echo "可执行文件: build/nteFishing.exe"
