# 电量协议与维护说明

核对日期：2026-10-26。实现位于 `src/device_protocol.cpp`，设备枚举与 Windows I/O 位于 `src/device.cpp`。无新增运行时依赖；型号表编译进程序，运行时不下载设备配置或网页代码。

## Gmouse

来源：[迈从官方 G 系列网页驱动](https://www.mchose.com.cn/gmouse/)，其 `getBatteryInfo`、`getConnectMode` 和回包监听逻辑。

Windows 输出缓冲区为 65 字节（含报告 ID 0），开头为 `00 55 30 A5 0B 2E 01 01 01`，其余为零。有效输入回包以 `00 AA 30` 开头，Windows 缓冲区偏移 9 为原始电量，偏移 10 为充电标记。

电量须在 0–100，充电标记须为 0 / 1。中间状态字节会变化，不将其固定为某次采样中的值。只访问 `FF01:0010` 厂商集合，不读取鼠标移动或按键集合。

## 取反 Feature

来源：[迈从官方驱动](https://www.mchose.com.cn/)的 `0x11 0x06` 命令、状态解析器和型号/连接表；与 [alexfrih/mchose-linux 的实测协议记录](https://github.com/alexfrih/mchose-linux/blob/main/PROTOCOL.md)及 [HaloBattery 的 M7 实测记录](https://github.com/HeyOkay/HaloBattery/blob/main/providers/mchose.py)交叉核对。

短 Feature 报告 ID 为 `11`，长报告 ID 为 `12`。电量查询命令为 `06`，命令和整个负载（含零填充）按位取反，报告 ID 保持不变。因此 Windows 请求开头为 `11 F9 FF ...`，长度按 HID 集合的 Feature 最大长度填充。先使用描述符声明的短报告，必要时尝试已声明的长报告。

响应中的命令必须为 `F9`。将偏移 2 开始的负载取反后，依次读取厂商、无线型号编号、固件、连接位、电量和充电状态。检查已知厂商和型号、无线模式、连接标记、电量范围和充电标记；拒绝查询回显及休眠空数据。每次重试重新发送查询，并使用可取消的短暂等待。

型号编号来自官方型号表，不能用接收器 PID 直接命名鼠标，例如 `1020` 被多个系列共享。AX5 V2 的旧编号 `00A1` 和新编号 `4010` 归并到同一型号。

## 第一代 A5 Pro Max

来源：[协议原作者的记录](https://github.com/z750sasr/mchose-a5-pro-max-web-driver/blob/main/docs/PROTOCOL.md)与 [HaloBattery 的 A5 接收器实测](https://github.com/HeyOkay/HaloBattery/blob/main/providers/mchose.py)。

使用报告 ID 0，Windows 缓冲区包含额外的报告 ID 字节。64 字节负载中的路由为 2、长度为 2、页为 0、命令为 `83`。查询回包必须包含 `A1` 标记并回显页及命令；负载偏移 6 为充电状态，偏移 7 为电量。

查询循环最多读取 8 次，中途最多重新发送一次，等待可取消。即便参考驱动将超范围电量裁到 100，本项目也将其视为无效数据，避免制造看似正常的电量。

## 资源与测试边界

- 共享原有后台工作线程；有效读数缓存接收器路径，不每分钟重建完整设备列表。
- 一轮查询使用 5 秒通信预算，单个候选最多占 3.5 秒；超时取消并回收未完成 I/O。设备枚举和驱动取消完成时间仍受 Windows 与设备驱动影响。
- Windows Feature 读写使用带事件的重叠 I/O；不通过同步 `HidD_GetFeature` 阻塞界面线程。
- 新测试覆盖设备/集合排除、原始电量边界、公开回包、休眠回显、重试上限、短/长报告切换和取消流程。
- 模拟测试不能替代实机传输验证。本机只有 G7；其他型号尚未由本项目实测。

以上协议事实用于独立实现，未引入上述项目的库或复制其实现文件。
