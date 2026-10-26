# MCHOSEBatteryTray

轻量级 Windows 鼠标电量托盘工具。当前版本 **1.1.0**，支持 **迈从 MCHOSE G7 的 2.4G 接收器**，使用 C++20 和原生 Win32 API 实现。

## 功能

- 在系统托盘显示鼠标电量；悬停查看百分比，双击查看详情。
- 电量图标随状态变化：50% 及以上为青绿色，25–49% 为黄色，低于 25% 为红色，充电时带闪电标记。
- 默认每 60 秒查询一次；设备离线或暂时不可读时，降为至少每 5 分钟一次。
- 默认在周一至周五 **20:59** 重新检查电量，**低于 25% 且未充电**时发送 Windows 通知。
- 支持自定义时间、阈值、查询间隔、星期、跳过日期和额外工作日期。
- 同一天最多提醒一次；当天错过检查可在启动、恢复运行或设备恢复可读时补检。
- 支持登录自启、手动刷新、测试通知和清理系统登记。
- 便携运行，配置、状态和日志集中保存在程序旁的 `data/` 中。

电量显示使用设备返回的原始百分比。官方网页版驱动会将数值分档显示，因此本程序显示 81–83% 时，官网可能显示 80%。本程序不为对齐官网而取整或平滑数值。

## 支持范围

- Windows 10 / 11，x64。
- MCHOSE G7，2.4G 连接。
- 已验证的接口：VID `A8A5`、PID `2255`、Usage Page `FF01`、Usage `0010`，输入和输出报告均为 65 字节。

目前只验证了以上设备，不代表其他迈从型号、有线连接或蓝牙连接也能读取。

## 使用

1. 从源码构建后，将整个 `build/portable/G7BatteryTray/` 文件夹放在可写目录。
2. 双击 `G7BatteryTray.exe`，图标会出现在任务栏右下角，也可能位于隐藏图标区域。
3. 悬停查看电量，双击查看详情，右键打开设置或刷新。
4. 使用右键菜单中的“测试 Windows 通知”检查通知通道。新配置默认关闭登录自启，可按需开启。

仓库名为 MCHOSEBatteryTray，当前程序文件名仍为 `G7BatteryTray.exe`。

提醒使用电脑本地时间。首次到期检查时，如果电量充足或正在充电，也会记录当天已检查，当晚不会追加提醒。程序退出或电脑关闭时无法检查，也不会主动唤醒电脑；跨天不补发。Windows 通知权限和勿扰设置会影响横幅显示。

## 构建与测试

需要 Windows、PowerShell 和 **LLVM-MinGW x64 UCRT** 工具链。当前版本使用 LLVM-MinGW `20261020` 构建，无需为运行程序另行安装 C++ 运行库。

```powershell
git clone git@github.com:hhmmLife/MCHOSEBatteryTray.git
cd MCHOSEBatteryTray
.\build.ps1 -ToolchainRoot 'D:\tools\llvm-mingw-20261020-ucrt-x86_64' -Tests
```

工具链可从 [LLVM-MinGW 官方仓库](https://github.com/mstorsjo/llvm-mingw) 获取。将示例中的路径替换为自己的工具链目录；若编译器已加入 PATH，可省略 `-ToolchainRoot`。

- 运行包：`build/portable/G7BatteryTray/`
- 编译和测试中间文件：`build/intermediate/`
- `-Tests` 执行时间与阈值、配置持久化、图标与 GDI 生命周期测试。
- 可用 `-OutputRoot` 指定运行包的绝对输出路径。
- 修改静态图标后，可执行 `assets/create-icon.ps1` 重新生成 `assets/app.ico`。

## 目录

```text
src/                         原生窗口、设备查询、通知、配置和托盘图标
assets/                      图标、资源和应用清单
tests/                       自动测试
third_party/                 nlohmann/json 及其许可证
build.ps1                    构建与测试入口
README.txt                   随运行包分发的使用说明
VERIFICATION.txt             验证记录与性能测量说明
THIRD-PARTY-NOTICES.txt      第三方许可声明
```

运行时生成的文件集中放在 EXE 同目录下：

```text
data/config.json             用户配置
data/state.json              当天检查与通知去重状态
data/logs/app.log             当前日志
data/logs/app.previous.log    上一份日志
data/probe.json               仅 --probe 生成的诊断结果
data/status.json              仅 --status 生成的运行状态
```

每份日志在约 256 KiB 时轮转，最多保留当前和上一份。JSON 使用同目录临时文件写入后原子替换。运行数据、编译产物及工具链不纳入 Git。

## 性能

常驻程序使用原生消息循环和事件等待，设备查询在工作线程执行。通知发送及其 COM/WinRT 初始化交给同一 EXE 的短暂子进程，完成后退出。图标使用小尺寸 DIB 绘制，无 GDI+ 依赖，不通过清空工作集压低内存读数。

2026-10-26 本机连续采样约 3 分钟、37 次，程序按 60 秒间隔完成 3 次查询：

| 指标 | 实测 |
| --- | --- |
| CPU 时间增量 | 0.03125 秒，约占单核 0.017% |
| 私有工作集 | 2.10 MiB，采样期间未增长 |
| 私有提交 | 2.83 MiB，采样期间未增长 |
| 句柄 / 线程 | 191 / 4，采样期间保持不变 |

这些数值来自单台电脑的短时测量，会随 Windows 版本、窗口操作和系统环境变化；不能替代长期稳定性验证。总工作集还包含共享 DLL 页面，与私有工作集不同。更多记录见 [VERIFICATION.txt](VERIFICATION.txt)。

## 移动与移除

退出后可移动整个运行目录。在新位置再次运行，程序会修复已启用的自启路径和通知路径。

通知和自启需要少量当前用户级 Windows 登记，不需要管理员权限。删除程序前，选择右键菜单中的“移除系统登记并退出”，再删除整个文件夹。普通退出保留登记和配置。登记位置及完整配置字段见 [README.txt](README.txt)。

当前已知限制：清理系统登记会在界面线程等待清理子进程，异常情况下可能等待约 15–16 秒；日常后台查询和通知发送使用工作线程。

## 命令行

| 参数 | 功能 |
| --- | --- |
| `--probe` | 查询一次，写入 `data/probe.json` 后退出，不登记通知或自启 |
| `--status` | 让运行中的实例写出 `data/status.json` |
| `--settings` | 打开设置 |
| `--show-details` | 显示详情，未运行时启动程序 |
| `--test-notification` | 发送测试通知，不改变每日提醒去重状态 |
| `--background` | 托盘启动，不打开窗口 |
| `--exit` | 退出运行中的实例 |
| `--unregister` | 清理本程序的系统登记并退出，保留配置和日志 |

## 参考与第三方依赖

- 表现形式参考 [InphicBatteryTray](https://github.com/Tan-TanZi/InphicBatteryTray)。图标和程序实现为本项目编写，未复制其素材。
- G7 电量协议与[迈从官方网页驱动](https://www.mchose.com.cn/gmouse/)的 `getBatteryInfo` 核对，仅查询厂商状态接口，不读取鼠标移动或按键数据，不修改 DPI、按键或灯光。
- 使用 [nlohmann/json](https://github.com/nlohmann/json) v3.12.0（MIT），许可证见 [third_party/LICENSE-json.txt](third_party/LICENSE-json.txt) 和 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)。
