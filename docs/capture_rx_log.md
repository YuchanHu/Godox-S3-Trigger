# 接收日志直接保存到文件

先关闭串口监视器，在项目目录打开PowerShell。

```powershell
./tools/capture_rx_log.ps1 -Port COM5 -Seconds 60
```

工具会读取状态、停止已有任务、启动全CH扫描，60秒后停止扫描并导出IQ。扫描期间多次按真实引闪器的TEST。文件保存到项目的`artifacts/rx-logs`，结束时打印绝对路径并校验16128个采样是否完整。

定位某一已知频道时，可用 `-Channel 14` 固定接收 CH14。省略该参数使用正常的自动扫频。
v0.6.3 的 IQ 导出优先保留复核阶段的 40 MS/s 采样，便于分析候选已出现但未入库的问题。
`python tools/decode_rx_dump.py 日志路径` 会分别列出完整帧与复核帧头的解码结果。

仅导出设备已经保留的采样可使用以下命令。

```powershell
./tools/capture_rx_log.ps1 -Port COM5 -DumpOnly
```

省略`-Port`时自动选择唯一的Espressif USB设备。工具使用现有ESP-IDF Python环境中的pyserial，处理固件命令行的终端光标查询，将收到的原始字节直接写入文件。终端仅显示进度。

开始扫描会清除上一轮的诊断采样。导出期间请等工具结束后再重新打开其他串口程序。中断、超时或设备拒绝导出时，已经收到的内容保存在文件中。

CH11/CH14排查时建议各保存一轮独立扫描，使用不同文件名。导出的是一次扫描中保留的一份候选IQ，需结合日志里的LO和实际解码结果判断它对应的CH。
