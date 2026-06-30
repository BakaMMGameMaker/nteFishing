# 异环钓鱼自动化工具 - CMake 构建配置
cmake_minimum_required(VERSION 3.16)
project(nteFishing LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用 vcpkg 工具链（由 cmake --preset 或 -DCMAKE_TOOLCHAIN_FILE 指定）
# 如果未指定，尝试自动寻找项目内的 vcpkg
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
endif()

# 查找 OpenCV（通过 vcpkg 安装，指定 root 避免多版本冲突）
if(DEFINED VCPKG_INSTALLED_DIR)
  set(OpenCV_ROOT "${VCPKG_INSTALLED_DIR}/x64-windows/share/opencv4")
endif()
find_package(OpenCV REQUIRED)

# 主程序
add_executable(nteFishing src/main.cpp)

# 链接 OpenCV
target_link_libraries(nteFishing PRIVATE ${OpenCV_LIBS})

# MSVC: 强制 UTF-8 编码以支持中文注释
if(MSVC)
  target_compile_options(nteFishing PRIVATE /utf-8)
endif()

# Windows 平台需要链接的系统库（SendInput 等）
if(WIN32)
  target_link_libraries(nteFishing PRIVATE user32 gdi32)
endif()

# 复制 img 目录到构建输出目录
add_custom_command(TARGET nteFishing POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${CMAKE_CURRENT_SOURCE_DIR}/img"
  "$<TARGET_FILE_DIR:nteFishing>/img"
  COMMENT "复制图像资源到构建目录"
)

# 复制 interception.dll 到构建输出目录
add_custom_command(TARGET nteFishing POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${CMAKE_CURRENT_SOURCE_DIR}/interception/interception.dll"
  "$<TARGET_FILE_DIR:nteFishing>/interception.dll"
  COMMENT "复制 interception.dll 到构建目录"
)
