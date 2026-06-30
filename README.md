# 🎣 异环钓鱼自动化工具 (nteFishing)

> 全自动钓鱼脚本，基于屏幕截图 + OpenCV 模板匹配 + Interception 内核级输入注入，适用于 UE5 游戏《异环》。

[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11-blue)]()
[![Language](https://img.shields.io/badge/Language-C%2B%2B17-orange)]()
[![License](https://img.shields.io/badge/License-MIT-green)]()
[![Game Engine](https://img.shields.io/badge/Game%20Engine-UE5-purple)]()

## ✨ 特性

- 🖥️ **屏幕截图识别** — GDI 高速截图 + OpenCV 模板匹配，无需注入游戏进程
- 🔌 **内核级输入注入** — 使用 Interception 驱动在 HID 层模拟按键/鼠标，绕过 UE5 RawInput 检测
- 🎯 **自适应点按跟随** — 实时跟踪绿色进度条位置，根据速度校准动态计算按键时长，精准高效
- 📊 **调试友好** — 匹配成功时输出屏幕坐标与置信度，方便调整阈值

## 🚀 快速开始

### 前提条件

- Windows 10/11 64 位
- 游戏分辨率 3200×2000（其他分辨率可能需要调整 `src/main.cpp` 中的搜索区域比例）
- 游戏窗口全屏或无边框窗口模式

### 第一步：安装 Interception 驱动

> 🔴 **必须步骤！** 异环是 UE5 游戏，使用 RawInput 直接读取硬件输入，普通的 `SendInput` 无法工作。

以**管理员身份**打开终端：

```powershell
cd <项目目录>
.\interception\install-interception.exe /install
```

然后**重启电脑**。

> 卸载驱动：`.\interception\install-interception.exe /uninstall`（同样需要重启）

### 第二步：编译

```bash
cmake -B build -S . -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### 第三步：运行

```bash
cd build
.\nteFishing.exe
```

> 💡 可能需要管理员权限（Interception 驱动要求）。

## 🎮 使用方法

1. 选择好鱼竿鱼饵，点击“开始钓鱼”
2. 运行 `nteFishing.exe`
3. 程序 5 秒倒计时，期间**将游戏窗口置于前台**（可以下载 PowerToys，Ctrl+Win+T 置顶该程序的同时不影响游戏窗口位于前台）
4. 程序自动循环：
   - 等待"准备钓鱼"提示 → 按 F 抛竿
   - 持续检测鱼上钩画面（最长 7.5 秒）→ 按 F 提竿
   - 自动跟随绿色进度条（自适应 `PressFor` 按键调整指针位置，进入死区或鱼上钩时松手）
   - 鱼捕获后 → 自动点击关闭结算界面
   - 回到第一步，等待下一次钓鱼

## 📁 项目结构

```
nteFishing/
├── src/                     # 源代码
│   ├── main.cpp             # 主程序（主循环状态机 + 业务逻辑）
│   ├── utils.h              # 通用工具（日志 + 图像加载）
│   ├── image_matcher.h      # 图像匹配器（屏幕截图 + OpenCV 模板匹配）
│   ├── follower.h           # 自适应指针跟随器（死区判断 + 速度校准 + 动态按键时长）
│   └── interception_driver.h  # Interception 驱动封装（内核 HID 注入）
├── img/                     # 模板图像
│   ├── ready_to_fish.png
│   ├── green_rect_left.png
│   ├── green_rect_right.png
│   ├── gold_cursor.png
│   ├── click_to_close.png
│   └── fish_caught.png
├── interception/            # Interception 驱动文件
│   ├── interception.dll     # 用户态 API DLL（运行时加载）
│   ├── interception.lib     # 静态链接库（备用）
│   ├── interception.h       # API 头文件
│   └── install-interception.exe  # 驱动安装器
├── .vscode/                 # VS Code 配置（构建任务 + 编辑器设置）
├── CMakeLists.txt           # CMake 构建配置
├── CMakePresets.json        # CMake 预设（编译器、工具链、vcpkg）
├── build.sh                 # 一键构建脚本
├── vcpkg.json               # 依赖清单 (opencv4)
└── vcpkg/                   # 自包含包管理器
```

## ⚙️ 配置参数

以下常量在 `src/main.cpp` 中可调：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `kMatchThreshold` | 0.80 | 模板匹配置信度阈值（`image_matcher.h`，TM_CCOEFF_NORMED） |
| `kDeadZone` | 50 | 指针到目标距离小于此像素数不按键（`follower.h`） |
| `kCalibDuration` | 0.1 | 首次移动校准按键时长，秒（`follower.h`） |
| `kMinDuration` | 0.05 | 单次按键最小时长，秒（`follower.h`） |
| `kMaxDuration` | 0.4 | 单次按键最大时长，秒（`follower.h`） |
| `kFishBiteTimeout` | 7.5 | 抛竿后等待鱼上钩最长秒数（`main.cpp`） |
| `kCheckInterval` | 0.3 | 鱼上钩检测间隔，秒（`main.cpp`） |
| `kMaxAttempts` | 5 | 进度条跟随失败最大重试次数（`main.cpp`） |
| 搜索区域（进度条） | 宽 30%-70%，高 4%-10% | `GetScreenArea` 参数 |
| 鱼上钩检测区域 | 宽 37.5%-62.5%，高 17.5%-25% | `FindTemplateInScreenRatio` 参数 |

## 🛠️ 技术细节

### 为什么不用 SendInput？

UE5 使用 **RawInput API** 直接从 USB HID 硬件读取键盘状态，完全绕过 Windows 消息队列。`SendInput`、`keybd_event`、`PostMessage` 等所有用户态 API 都无法到达游戏层。

### Interception 驱动原理

```
物理键盘 → USB 控制器 → HID 类驱动 → win32k.sys → 应用程序
                                   ↑
Interception 驱动 ← 我们的模拟输入（在此注入）
```

输入在 HID 驱动层注入，操作系统和应用程序都无法将其与真实硬件输入区分。

### 模板匹配策略

- 使用 `TM_CCOEFF_NORMED`（归一化相关系数），对亮度变化不敏感
- 构造时预加载所有模板图像到缓存（`unordered_map<路径, NTEAutoFishing::Image>`）
- `FindTemplatesInScreenRatio` 一次截图批量匹配多张模板，返回 `unordered_map<路径, optional<FoundImg>>`
- `green_rect_left.png` / `green_rect_right.png` 分别匹配绿色矩形的左、右边缘，联合检测可计算出完整矩形中心，同时兼容仅单侧匹配到时的降级跟随
- 搜索区域限定在屏幕比例范围内，减少误匹配和计算量

### 自适应点按跟随

- `Follower` 首次移动使用固定时长 `kCalibDuration`(0.1s) 校准基础速度
- 后续每轮根据实际像素位移与按键时长更新速度估计（运行平均值），逼近真实速度
- 根据当前距离和速度估算最优按键时长，钳制在 [0.05s, 0.4s] 防止过短或超调
- 调用 `PressFor` 按键，按下→保持→释放，下一轮重新截图评估
- 仅以下情况松手（不按键）：
  1. 指针进入死区（`|target - cursor| < 50`）
  2. 检测不到金色指针且两个绿色矩形都缺失（鱼可能已上钩）

## ⚠️ 注意事项

- **仅供个人学习研究使用**，请勿用于商业用途
- 用别怕怕别用；但果体 pak mod 都懒得管了，不至于管钓鱼脚本吧（小声）
- 不同分辨率需要调整 `src/main.cpp` 中的屏幕比例参数
- 游戏 UI 改版后可能需要更新 `img/` 下的模板图像（call 我或者微信截图手动更新）

## 📄 许可协议

MIT License

---

*Made with ❤️ for lazy fishermen*

Bilibili：郁娇天使
代表超天酱对宅宅们表示爱意