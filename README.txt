MCHOSEBatteryTray 1.2.0
迈从鼠标 2.4G 电量托盘程序（Windows 10/11 x64）
程序文件名继续使用 G7BatteryTray.exe，兼容原来的配置、自启与通知登记。
实现 27 个型号变体的协议；本项目目前只有 G7 完成本机实测。
其他型号完成协议与模拟通信测试，仍待实机验证。完整范围见 COMPATIBILITY.md。

使用
1. 将整个 G7BatteryTray 文件夹放在一个可写的固定目录，双击 G7BatteryTray.exe。
2. 图标位于任务栏右下角，可能在“显示隐藏的图标”里。可手动拖到常显区域。
3. 悬停查看电量；双击查看详情；右键设置、刷新或退出。
4. 右键“测试 Windows 通知”可检查提醒通道；新配置中“开机自动启动”默认关闭，已有配置保留选择。

默认提醒
每周一到周五，本地时间 20:59 重新查询鼠标电量。
仅在有效电量 <25% 且未充电时提醒，25% 不提醒，同一天最多一次。
首次检查时电量足够或正在充电，也会记录当天已检查；当晚不会因为继续耗电而追加提醒。
当天错过检查会在应用启动/电脑恢复/鼠标重新可读时补检，跨天不补发。
电脑关闭或程序退出时不能检查；程序不主动唤醒电脑。
通知遵循 Windows 通知权限和勿扰设置；提交成功不代表系统一定弹出横幅。
节假日和调休需在设置中维护本地例外日期；不连接网络日历。
同时填写为跳过和补班时，跳过优先。

目录
G7BatteryTray.exe                程序，不依赖额外运行时安装
README.txt                      本说明
THIRD-PARTY-NOTICES.txt          开源许可
data/config.json                配置（首次启动自动生成）
data/state.json                 当天检查/通知去重记录（首次需要时生成）
data/logs/app.log                状态变化和错误，256 KiB 上限附近自动轮转
data/logs/app.previous.log       上一份日志，最多保留一份
data/probe.json                 仅 --probe 诊断命令生成
data/status.json                仅 --status 诊断命令生成
JSON 写入使用同目录 .tmp 临时文件，成功后原子替换。
程序只访问支持型号的厂商状态接口，不读取鼠标移动或按键数据，不修改 DPI、按键或灯光。
按接收器 VID/PID、厂商集合、报告长度与报告 ID 筛选，三类协议独立解析。
原始百分比不取整或平滑；错误报文和休眠空响应不会显示为 0%。
单个托盘图标同时监控一只鼠标，优先上次成功的接收器；每日提醒仍按当前鼠标去重。
设备路径运行时枚举，不与某个 USB 插口或某台电脑绑定。

低内存设计（1.2.0）
图标用小尺寸 DIB 原生绘制，不加载 GDI+ 或 PNG 解码组件；字体按窗口需求创建和释放。
设备接口由 Configuration Manager 轻量枚举，不建立完整 SetupAPI 设备上下文。
常驻进程不初始化通知 COM/WinRT；通知发送、身份修复、通知点击回调由同一个
EXE 的短暂进程处理，结束后退出，不新增 EXE 或请求缓存文件。
主界面线程仅编辑数字、时间和 ISO 日期，因此不初始化该线程的输入法。
此设置只针对本程序线程，不改变其他软件或系统输入法配置；支持粘贴日期。
没有使用 EmptyWorkingSet 或循环清空工作集来降低显示数值。
内存请区分“私有工作集”“总工作集”“私有提交”，具体实测见 VERIFICATION.txt。

颜色和读取
>=50% 青绿，25–49% 黄色，<25% 红色，充电有闪电标记。
默认 60 秒查询；未连接或暂不可读时降低为至少 5 分钟一次。
临近当天待完成的提醒会每分钟重试一次，直到读到有效电量或跨天。
USB 插拔和恢复运行时立即尝试读取。
读不到会灰显，不会伪装成 0%；有历史值时悬停仍显示上次电量和时间。

移植与移除
退出程序后搬走整个目录；在新位置运行一次，自动修复已启用的自启路径和通知路径。
Windows 通知和自启需要少量用户级系统登记，不需要管理员权限：
- 开始菜单：%APPDATA%\Microsoft\Windows\Start Menu\Programs\G7BatteryTray.lnk
- 通知 COM：HKCU\Software\Classes\CLSID\{E9C2D221-F9CF-47DD-B821-4C12CE418965}
- 通知标识：HKCU\Software\Classes\AppUserModelId\G7BatteryTray.Desktop
- 自启（仅开启时）：HKCU\Software\Microsoft\Windows\CurrentVersion\Run\G7BatteryTray
卸载前选择“移除系统登记并退出”，然后删除整个目录。
普通“退出”保留登记和设置；关闭自启只删除自启项。
Windows 自身维护的通知设置、缓存、崩溃记录由操作系统管理。

命令行
--probe                 查询一次电量，写入 data/probe.json 后退出；不登记自启/通知
--show-details          显示运行中实例的详情；未运行时启动程序
--settings              打开设置窗口
--status                将运行中实例的实时状态写入其 data/status.json
--test-notification     发送一条测试通知，不改变提醒去重记录
--exit                  退出运行中实例
--background            正常托盘启动，不打开窗口
--unregister            移除本程序的系统登记并退出，保留 data 中的设置和日志

配置字段（通常直接通过右键设置修改）
schemaVersion: 1
remindersEnabled: true
startupEnabled: false
reminderTime: "20:59"
lowBatteryThreshold: 25
pollSeconds: 60（允许 15–3600）
weekdays: [1,2,3,4,5]（0=星期日，6=星期六）
skipDates: ["2026-10-01"]
extraWorkDates: ["2026-10-10"]
手动编辑前先退出程序；无效配置会报错并保留原文件。

构建（源码包）
工具链：LLVM-MinGW x64 UCRT，使用 Windows 系统 API，静态链接 C++ 运行库。
PowerShell：
  .\build.ps1 -ToolchainRoot '你的 llvm-mingw 目录' -Tests
默认产物位于源码包 build/portable/G7BatteryTray，编译中间文件位于 build/intermediate。
可用 -OutputRoot 指定运行目录。工具链本身不随运行包分发。
修改图标后可执行 assets/create-icon.ps1 重新生成图标资源。
构建后运行 package.ps1，在 dist 中生成运行包、源码包与校验文件，自动排除用户运行数据。

设计参考：https://github.com/Tan-TanZi/InphicBatteryTray
设备协议核对：迈从官方 https://www.mchose.com.cn/gmouse/ 的 getBatteryInfo。
其他协议来源见 PROTOCOLS.md；具体型号和验证程度见 COMPATIBILITY.md。
图标和程序实现为本项目编写，未复制参考项目的素材。
