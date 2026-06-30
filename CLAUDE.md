# 异环钓鱼自动化工具

## 项目概述

通过屏幕截图 + OpenCV 模板匹配识别游戏 UI 元素，通过 **Interception 内核驱动**在 HID 层注入键盘/鼠标事件，实现钓鱼流程全自动化。

> **关键：** 异环是 UE5 游戏，使用 RawInput API 直读 HID 硬件。`SendInput` 注入的 Windows 消息无法到达游戏层。Interception 驱动在内核 HID 设备层注入，游戏无法区分真实按键。

## 编译环境

| 组件 | 路径 | 版本 |
|------|------|------|
| C++ 编译器 | `E:\LLVM\bin\clang++.exe` | 21.1.6 (MSVC ABI) |
| MSVC 工具链 | `E:\MicrosoftVisualStudio\VC\Tools\MSVC\14.50.35717\` | 14.50 |
| Windows SDK | `C:\Program Files (x86)\Windows Kits\10\` | 10.0.26100.0 |
| CMake | `C:\Program Files\CMake\bin\cmake.exe` | 4.1.2 |
| Ninja | `E:\LLVM\bin\ninja.exe` | (随 LLVM) |
| vcpkg | `项目目录\vcpkg\` | 2026-05-27 |
| OpenCV | vcpkg 安装 | 4.12.0 |
| Interception | `项目目录\interception\` | 1.0.1 |

## 网络

- 代理: `127.0.0.1:7890` (Clash/V2Ray，HTTP 类型)
- vcpkg 自动读取 IE 代理设置；环境变量 `HTTP_PROXY`/`HTTPS_PROXY` 设为 `http://127.0.0.1:7890`
- 下载偶发 SSL 错误（curl error 35），重试即可

## 首次配置

### 1. 安装 Interception 驱动（仅一次）

以**管理员身份**打开终端：
```powershell
.\interception\install-interception.exe /install
```
然后**重启电脑**。

### 2. 构建

**方式一：CMake Presets（推荐，命令行 + VS Code 通用）**

```bash
# 设置 MSVC 环境变量（每次新终端都需要）
export PATH="E:/LLVM/bin:E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/bin/Hostx64/x64:/c/Program Files/CMake/bin:$PATH"
export INCLUDE="E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/include;E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/atlmfc/include;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/shared;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/um;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/winrt"
export LIB="E:/MicrosoftVisualStudio/VC/Tools/MSVC/14.50.35717/lib/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/um/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0/ucrt/x64"

# 配置（仅首次或 CMakeLists.txt 变更后需要）
cmake --preset default

# 编译
cmake --build --preset default
```

**方式二：VS Code 快捷键**
- `Ctrl+Shift+B` → 默认构建（调用 `cmake --build --preset default`）
- 首次使用需先运行 `CMake 配置` 任务（`Ctrl+Shift+P` → `Tasks: Run Task` → `CMake 配置`）
- VS Code 任务已内置 MSVC 环境变量，无需手动设置终端环境

**方式三：CMake Tools 扩展**
- 安装 VS Code 的 CMake Tools 扩展后，自动读取 `CMakePresets.json`
- 状态栏点击 Build 按钮即可

**方式四：传统命令行（备用）**

```bash
cmake -B build -S . -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 3. 运行

```bash
cd build
.\nteFishing.exe
```

程序可能需要**管理员权限**（Interception 驱动要求）。

## 项目结构

```
nteFishing/
├── src/                      # 源代码
│   ├── main.cpp              # 主程序（业务逻辑 + 主循环状态机）
│   ├── follower.h            # 指针跟随器（死区判断 + 方向计算）
│   └── interception_driver.h # Interception 驱动封装（内核 HID 注入 + 输入模拟）
├── img/                      # 模板图像
│   ├── ready_to_fish.png     # "准备钓鱼"提示
│   ├── green_rect_left.png   # 绿色矩形左边缘
│   ├── green_rect_right.png  # 绿色矩形右边缘（与左边缘尺寸接近）
│   ├── gold_cursor.png       # 金色指针
│   ├── click_to_close.png    # "点击关闭"按钮
│   └── fish_caught.png       # 鱼上钩提示（阶段2检测用）
├── interception/             # Interception 驱动文件
│   ├── interception.dll      # 用户态 API DLL
│   ├── interception.lib      # 静态链接库（未使用，动态加载）
│   ├── interception.h        # API 头文件
│   └── install-interception.exe  # 驱动安装器
├── CMakeLists.txt            # 构建配置
├── CMakePresets.json         # CMake 预设（编译器、工具链、环境变量）
├── .vscode/                  # VS Code 配置
│   ├── tasks.json            #   Ctrl+Shift+B 构建任务
│   └── settings.json         #   编辑器 + CMake Tools 设置
├── vcpkg.json                # 依赖清单 (opencv4)
├── vcpkg/                    # 包管理器（git clone，自包含）
├── vcpkg_installed/          # 编译好的依赖
└── build/                    # 构建产物
    ├── nteFishing.exe
    ├── interception.dll      # 运行时复制
    └── img/                  # 运行时图像资源副本
```

## 代码架构

### 文件职责

| 文件 | 职责 |
|------|------|
| `src/interception_driver.h` | Interception 驱动类型定义、`InterceptionDriver` 类、全局实例声明、`Click`/`WaitFor`/`PressFor` 内联函数 |
| `src/follower.h` | 指针跟随器：`FollowAction` 结构、`Follower` 类（死区判断 + 方向计算） |
| `src/main.cpp` | 业务逻辑：屏幕截图、OpenCV 模板匹配、主循环状态机、持续按压跟随循环 |

### 数据结构
- `FoundImg` — 模板匹配结果（坐标 + 模板尺寸 + 置信度），定义在 `src/main.cpp`
- `FollowAction` — 跟随动作（方向），定义在 `src/follower.h`
- `Follower` — 指针跟随器类（死区判断 + 方向计算），定义在 `src/follower.h`
- `InterceptionKeyStroke` / `InterceptionMouseStroke` — HID 输入事件结构，定义在 `src/interception_driver.h`
- `InterceptionDriver` — 动态加载 `interception.dll`，封装内核级输入注入，定义在 `src/interception_driver.h`

### 核心函数

**业务逻辑（src/main.cpp）**

| 函数 | 功能 | 实现方案 |
|------|------|----------|
| `GetScreenWidth/Height` | 获取屏幕尺寸 | `GetSystemMetrics(SM_CXSCREEN)` |
| `GetScreenArea` | 截取屏幕比例区域 | GDI `BitBlt` → `cv::Mat`，内部换算比例→像素 |
| `GetImg` | 从文件加载图像 | `cv::imread` |
| `ImgPosition` | 模板匹配 | `cv::matchTemplate` + `cv::minMaxLoc` |
| `FindTemplate` | 在截图中查找模板 | 调用 `ImgPosition` + 输出匹配日志 |
| `FindTemplateInScreenRatio` | 在屏幕比例区域内查找模板 | 组合 `GetScreenArea` + `FindTemplate` |

**跟随器（src/follower.h）**

| 函数 | 功能 | 实现方案 |
|------|------|----------|
| `Follower::Follow` | 死区判断 + 方向计算 | `|TargetX - CurrentX| < 10` → `'\0'`；否则返回 `'A'`/`'D'` |

**驱动封装（src/interception_driver.h）**

| 函数 | 功能 | 实现方案 |
|------|------|----------|
| `InterceptionDriver::SendKey` | 发送键盘事件 | 内核 HID 注入（PS/2 扫描码） |
| `InterceptionDriver::SendLeftClick` | 发送鼠标左键事件 | 内核 HID 注入 |
| `Click` | 鼠标左键点击 | `SendLeftClick` 按下→50ms→释放 |
| `PressFor` | 按指定键保持 N 秒 | `SendKey` 按下→Sleep→释放（扫描码映射: F=0x21, A=0x1E, D=0x20） |
| `WaitFor` | 等待 N 秒 | `std::this_thread::sleep_for` |

### 主循环逻辑
1. 等待 `ready_to_fish.png` 出现（屏幕右下 75%-100% 区域）→ 按 F 开始钓鱼
2. 持续检测 `fish_caught.png`（屏幕 37.5%-62.5% 宽, 17.5%-25% 高）判断鱼是否上钩，最长等待 7.5 秒 → 按 F 提竿
3. 等 0.5 秒 UI 出现 → **持续按压跟随循环**（~30ms/次）：
   - 截图屏幕顶部区域 (30%-70% 宽, 4%-10% 高)，同一帧匹配 `green_rect_left`、`green_rect_right`、`gold_cursor`（阈值 0.80）
   - **松手条件：** gold_cursor 缺失 **或** 两个绿色矩形都缺失 → 释放按键 → 等 2.5s → 检测 `click_to_close.png` → 点击关闭或重试
   - 两侧都找到 → 跟随完整矩形中心 = `(leftX + rightX + rightWidth) / 2`
   - 仅左侧 → 降级跟随左侧右边缘 + Log
   - 仅右侧 → 降级跟随右侧左边缘 + Log
   - `Follower::Follow(target, cursor)` 返回方向 → 死区内松手，死区外**按住对应方向键不放**（方向变化时自动切换按键）
   - 重试最多 5 次，超时丢弃本次钓鱼
4. 回到步骤 1，等待下一次钓鱼

> **持续按压 vs 断续点按：** 旧方案每轮 `PressFor`（按下→计算时长→松开），存在松手期间的跟随死区。新方案只在条件触发时才松手，其余时间始终按住 A 或 D，响应更快、无超调。

### 关键常量
| 常量 | 值 | 位置 | 说明 |
|------|-----|------|------|
| `kMatchThreshold` | 0.80 | `main.cpp` | 模板匹配置信度阈值 |
| `kDeadZone` | 10 | `follower.h` | 指针跟随死区（像素） |
| `kFishBiteTimeout` | 7.5 | `main.cpp` | 抛竿后等待鱼上钩最长秒数 |
| `kCheckInterval` | 0.3 | `main.cpp` | 鱼上钩检测间隔（秒） |
| `kMaxAttempts` | 5 | `main.cpp` | 阶段 3 指针跟随失败最大重试次数 |
| 按键 F/A/D 扫描码 | 0x21 / 0x1E / 0x20 | `interception_driver.h` | PS/2 扫描码 |
| 鱼上钩检测区域 | (0.375,0.175)-(0.625,0.25) | `main.cpp` | 屏幕比例坐标 |
| 跟随检测间隔 | ~0.03 | `main.cpp` | 持续按压循环间隔（秒） |
