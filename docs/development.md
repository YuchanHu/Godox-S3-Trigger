# 开发与发布

## 开发环境

固件固定使用 ESP-IDF v5.5.5。激活该版本环境后，按根目录 README 构建。
开发容器固定到同版本镜像。容器内可编译固件，射频与墨水屏操作需要真实硬件。
编辑器中的 SDK 路径和设备端口由开发者本地配置。

工具依赖使用普通 Python 环境安装。

```sh
python -m pip install -r tools/requirements.txt
```

固件构建使用已经提交的字形和波形资源。重新生成这些资源与运行测试时才需要 Python 工具依赖。

## 测试

Windows 主机回归使用 PowerShell、原生 Windows TinyCC、Python 和上述依赖。
从 [TinyCC 项目](https://bellard.org/tcc/) 获取 Windows 编译器，解压后传入可执行文件路径。

```powershell
./tools/test_stage_host.ps1 -Compiler C:/tools/tcc/tcc.exe
```

脚本会编译真实 C 协议、DSP、UI、缓存与接收队列代码，在 `artifacts/` 写入主机 DLL，
然后运行 Python 回归。Win32 队列模型使用 Windows API，因此完整主机套件在 Windows 执行。
116 项是 v0.6.5 开发环境的记录，可选研究 checkout 未取得时，相应对比测试会显示 skipped。

自包含波形验证和 UI 图片复现可分别执行。

```sh
python tools/verify_waveform.py
python tools/render_ui.py --check
```

实际 UI 渲染工具支持原生 GCC、Clang、TinyCC。图像生成说明见 [images/README.md](images/README.md)。
SDK 编译后还需执行 README 中的 ELF 检查，以确认射频 SRAM 保留与嵌入波形。

## 目录与生成文件

- `sdkconfig.defaults` 提供共享配置，`sdkconfig` 由 ESP-IDF 本地生成。
- `build/`、`artifacts/`、日志、Python 缓存及本地研究 checkout 由 Git 忽略。
- 波形 `.bin` 和 `tests/fixtures/*.bin` 属于必需资源，必须进入仓库。
- 根目录旧构建日志整理到本地 `artifacts/build-logs/`。
- README 配图使用当前 C 渲染，历史浏览器原型保留在本地。
- 历史验证文件保留开发时的路径与设备信息，用于解释验证来源。它们引用的本地 `artifacts/` 原始日志和旧二进制未纳入源码发布。

## 发布到 GitHub

1. 从干净源码执行构建、ELF 检查、主机测试和 UI 图片一致性检查。
2. 提交源码、默认配置、必要资源、图片和第三方许可证。
3. 核对 `CMakeLists.txt` 版本与 README 验证范围。
4. 在自己的 GitHub 仓库创建发布版本。二进制附件应包含应用、bootloader、分区表、对应源码版本和 SHA256。
5. 使用 ESP-IDF 构建生成的刷写地址和参数。波形资源单独留在源码中。

可用以下命令根据 Git 忽略规则生成源码压缩包，适用于首次初始化仓库前或已有仓库。

```sh
python tools/package_source.py
```

输出到 `artifacts/Godox-S3-Trigger-v<版本>-source.zip`。工具使用临时 Git 元数据核对文件范围，
检查波形、采样夹具、字体和许可证是否进入归档。生成后建议解压到干净目录重新构建。

项目自身采用 GPL-3.0-or-later。字体、第三方驱动与 EPD 数据表保留各自授权和署名。
SDK 与闭源 PHY 由用户安装的 ESP-IDF 提供，来源及范围见 [第三方说明](../third_party/README.md)。
