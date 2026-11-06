# 固件 UI 图片

这些图片直接来自 `components/stage_ui/stage_view.c` 的 `stage_view_render()`，使用固件内嵌字形。
`tools/render_ui.c` 构造示例状态，调用实际渲染函数输出 200 × 200 单色帧缓冲。
`tools/render_ui.py` 仅转换 PBM 到 PNG 并拼接总览，每个页面保持原始像素。

| 图片 | 页面 |
| --- | --- |
| home.png | 主菜单 |
| control.png | 引闪控制 |
| editor.png | CH 数字键盘 |
| joint.png | 共同引闪 |
| scan.png | 正在接收及已发现组合 |
| running.png | 运行计数 |
| group.png / id.png / power.png | 分组、无线ID和功率编辑 |
| count.png / interval.png | 次数和间隔编辑 |
| preparing.png | 波形准备进度 |
| scan-empty.png | 等待接收信号 |
| targets.png / targets-page2.png / targets-empty.png | 组合查看、翻页和清空后状态 |
| joint-running.png | 共同引闪轮次 |
| done.png / stopped.png | 完成和手动停止 |
| prepare-error.png | 准备失败示例 |
| sleep.png | 休眠全白屏 |
| manual.png / manual-saved.png / manual-duplicate.png | 手动添加、保存成功和重复组合提示 |

电量、参数、组合和计数用于展示界面，来自可复现的示例状态。
各页面操作说明见 [功能操作图解](../user-guide.md)。

安装 `tools/requirements.txt` 和原生 C 编译器后，在项目根目录运行。

```sh
python tools/render_ui.py
python tools/render_ui.py --check
```

支持 `cc`、GCC、Clang 和 TinyCC，通过 `--compiler PATH` 指定编译器。
图片修改随固件 UI 重新生成，README 总览使用 `overview.png`。
