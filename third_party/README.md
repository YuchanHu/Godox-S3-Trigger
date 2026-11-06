# 来源、固定版本与许可

当前发布基线为v0.6.5。下文按引入版本记录来源与修改历史。
当前固件启用8MB PSRAM接收队列，CH14 / ID OFF已通过实机接收验证，详见根目录README。

## v0.6.0 候选检测与流水线参考

[Ran-ying/esp32-godox-flash](https://github.com/Ran-ying/esp32-godox-flash)，固定提交
`c05b3a99139216fc5a53d371445a7ecc348fe88a`，GPL-3.0-or-later。
参考候选优先、双任务队列设计，接收频率变体表依据`main/dsp/channels.inc`。
本地实现九段FFT检测、六频段轮转、IQ8队列及本地协议验证，适配ESP-IDF5.5.5。
来源在`components/godox_protocol/godox_scan.c`标注，项目根目录GPL许可覆盖本地GPL组件。

## v0.5.0 原生RX与本地频道切换

RX突发采样参考 [ESPARGOS/esp-sdr](https://github.com/ESPARGOS/esp-sdr)，提交
`9b61e7465bee6da15209c5c09308043d08c46a1d`，GPL-3.0-or-later。完整原始S3文件、许可和适配边界
保存在 `esp-sdr-reference/`，参考文件不参与构建。本地只适配64KiB的16MS/s快照路径，
不导入192KiB连续采样环；GFSK判频、Godox校验与组合管理为本地实现。
原始RX验证与本项目v5.5.5的适配验证分开记录；本项目当前接收验证见 `docs/validation-current.json`。

本地IQ TX增加仅停止状态下修改频道的S3入口，下次PLAY仍经过原来的PHY准备与NCO清除，
未改变RF窗口和SRAM地址；跨频道工作仍待实测。

## v0.4.0 当前数字字形

[Spleen](https://github.com/fcambus/spleen)，固定提交 `5116f8eac7f65da2cfa169626ec43b5d041665fe`，
Copyright (c) 2018–2026, Frederic Cambus，BSD-2-Clause。原生8×16、12×24、16×32 BDF与完整许可
保存在 `assets/fonts/spleen/`。固件只嵌入ASCII子集，不缩放或重绘源点阵；源码BDF未修改。
原提交信息保存在 `source.json`。数字、数据值使用Spleen，中文、描述英文及运行摘要使用下述秋叶16px。

## v0.3.0 引入的描述字体与电池

描述字形使用用户提供的 `assets/QiuYeYuanTi-16.ttf`，内嵌版权为yzdnn、Keitarou Hiraki/Font Silo，
SIL OFL 1.1。原字体不修改；嵌入的StageUI Bitmap子集保留许可于 `assets/QiuYeYuanTi-16.LICENSE.txt`。
SHA256 `179b02c56ffc8683dab39a5e54fc5cc1613fb86e8e7b5f255cd674cda2ac03cc`。
下方Noto来源仅为v0.2.0历史记录，不是当前字体生成依赖。
电池ADC引脚、分压和估算端点按同一Waveshare提交的 V2/01_ADC_Test 与
V2/11_FactoryProgram/port_adc.cpp 核对，采样/滤波/错误处理代码为本地实现。

## v0.2.0 新增屏幕与字形

- [Waveshare 触摸板示例](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)，研究提交
  `aaf395c1bbd86f7c5b7a2f9f0401241ff5c44956`。引脚/电源极性/FT6336坐标寄存器来自
  `02_Example/ESP-IDF/V2/13_FT6336_Test/main` 及 V2/11_FactoryProgram 的配置。
  未把未声明许可的板卡包装类直接导入工程。
- [Waveshare EPD_1in54_V2.c](https://github.com/waveshareteam/e-Paper/blob/master/RaspberryPi_JetsonNano/c/lib/e-Paper/EPD_1in54_V2.c)：
  MIT式许可；完整原文件和许可保存在 `waveshare-epd/EPD_1in54_V2.c`，用于追踪LUT和寄存器序列。
  `components/stage_ui/epd_lut.h` 为该文件数据表的提取；SPI/任务/超时管理为本地实现。
- 中文字形：Noto Sans CJK SC Regular（StageUI Bitmap 栅格子集），SIL OFL 1.1。
  许可与原版权见 `ui-font/OFL.txt`；不修改或依赖用户系统字体。
  [原字体下载](https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf)，
  SHA256 `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`。
  生成脚本 `tools/generate_ui_font.py`；C字形数据使用OFL许可，其他新增固件代码使用GPL-3.0-or-later。
- TinyCC仅为本机主机测试工具，位于被忽略的 artifacts，不是固件或工程分发依赖。

新运行时DSP为本地数学实现：`godox_dsp.c` / `dsp_tables.h`；长帧裁剪、门控和CH映射的验证边界见
`docs/user-guide.md`。上游RF寄存器序列沿用本文件下方所列的固定版本。

## 实际编入固件的驱动

[whstudio123/esp32-iq-tx](https://github.com/whstudio123/esp32-iq-tx)，固定提交
`cf88cb6ab367c9db44d5af81d85923cfc883751e`，GPL-3.0-or-later。
`esp32_iq_tx/` 保存其 S3 源码、公共 API、LICENSE、README、署名说明和来源映射。
原作者署名与 SPDX 保留；本项目自写部分同样按 GPL-3.0-or-later 提供。
ESP-IDF 和 Espressif 闭源 PHY 库由用户的 SDK 提供，保留各自条款。

本地修改（2026-11-04）：

- 增加 `iq_tx_deinit`，停止 RF 后释放 PHY、Wi-Fi 外设时钟、电源域，并清除 API 状态。
- 修正 Wi-Fi 初始化中间失败的释放路径；失败后应用禁止再次发射，须重启。
- 增加 S3 `iq_tx_play_bounded`：厂家 PHY/CW 准备在临界区外；从 PLAY 置位之前进入临界区，380 µs 后执行原 stop，记录含关闭开销的窗口。
- 加入 esp_timer 依赖；寄存器地址、NCO 清除、DAC 配置及原启动/停止顺序保持上游实现。
- 本地扩展只支持 S3；未导入 S31 源码。

上游此版本 README 的 S3 测试 SDK 是 **v5.3.5**。本项目按用户要求固定 **v5.5.5**，
编译/链接兼容和硬件兼容分开验收。新的窗口控制和重复 init/deinit 尚无实板证据。

## 协议与离线参考

[mildsunrise/godox-flash-radio](https://github.com/mildsunrise/godox-flash-radio)，
本地研究版本 `07849bb9796e7aeb12fca8404471f27211d983ca`。
该提交未发现 LICENSE，也未发现 transmitter.py 中的许可声明；不推断其为 MIT 等许可证。
`tools/godox_model.py` 根据所述协议和数学算法独立实现，低通系数由公式生成，未复制该源文件。
本地只读研究 checkout `godox-flash-radio/` 已被 gitignore 排除，不作为本项目授权再分发的源文件。
自动测试会导入它截取帧和基带输出，做逐字节比对。新机器可自行获取用于研究：

```powershell
git clone https://github.com/mildsunrise/godox-flash-radio.git third_party/godox-flash-radio
git -C third_party/godox-flash-radio checkout 07849bb9796e7aeb12fca8404471f27211d983ca
```

未获取时三项参考对比测试明确显示 skipped；其余自包含测试仍运行。
发布参考源文件前应向作者核实许可。

交叉阅读（没有导入代码）：

- [Martin Sivák 的 Godox X 接收器分析](https://marsik.codeberg.page/godox-olympus-protocol-article/)
- [antono / bitDealer 的 X1T 空中帧记录](https://gist.github.com/antono/5fe6c459fea414dedf1aa4981794ec3e)

旧 X1T gist 中的频道与位极性描述不能直接替代当前所用的 CH08 实测参考；本项目按 mildsunrise 的
CH08=2424.5 MHz、负偏移表示 1、81 位短帧实现，不混用 nRF24 的字节取反规则。
