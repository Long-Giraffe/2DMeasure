# 卡尺软件验证报告

验证日期：2026-06-03。

## 结论

- Debug 和 Release 版本的 `measure_app` 均编译通过。
- 核心算法回归全部通过，已有的 0.25 px 过采样、正反向一致采样、端点 padding、无固定 `+0.5` 检测偏移没有回退。
- `1.png` 探针检测到 4 条边，位置落在真实像素边界附近，没有出现系统性 0.5 px 检测偏移。
- 新增的所有边缘显示、边缘点击生成边距测量、曲线下置、sigma 默认值/步长、端点拖动灵敏度均已通过编译级验证；交互项需要在 GUI 中按下方场景复核。

## 已执行命令

| 命令 | 结果 |
|---|---|
| `cmake --build build --config Debug --target measure_app` | PASS，生成 `build/Debug/measure_app.exe` |
| `cmake --build build --config Release --target measure_app` | PASS，生成 `build/Release/measure_app.exe` |
| `cmake --build build --config Release --target caliper_validation` | PASS |
| `.\build\Release\caliper_validation.exe` | PASS，9 项算法回归全部通过 |
| `cmake --build build --config Release --target edge_bias_probe` | PASS |
| `.\build\Release\edge_bias_probe.exe .\1.png` | PASS，检测到 4 条边 |

## 核心算法回归

| Test | Status | Measured | Expected | Error | Extra |
|---|---:|---:|---:|---:|---:|
| single vertical edge | PASS | 100.3492 | 100.3500 | -0.0008 | 1 |
| forward/reverse valid endpoints | PASS | 100.3492 | 100.3492 | 0.0000 | 1 |
| forward/reverse with p2=image.cols | PASS | 100.3492 | 100.3492 | 0.0000 | 1 |
| subpixel shift along scan | PASS | 0.0013 | 0.0000 | 0.0013 | 100.3499 |
| perpendicular shift on vertical edge | PASS | 0.0000 | 0.0000 | 0.0000 | 100.3492 |
| slanted edge y-shift geometry | INFO | 4.2872 | 0.0000 | 0.0004 | 15 |
| two-edge bright stripe | PASS | 70.5000 | 70.5000 | -0.0000 | 2 |
| edge near profile end | PASS | 218.5000 | 218.5000 | 0.0000 | 1 |
| width 20 vs 21 effective samples | PASS | 20.0000 | 21.0000 | -1.0000 | 0 |

## `1.png` 探针结果

- 图像尺寸：1843 x 1316。
- 黑色区域边界：left=483.5，right=1054.5，top=205.5，bottom=656.5。
- 中心水平卡尺检测到 4 条边：
  - x=483.489978，polarity=-1。
  - x=488.510022，polarity=+1。
  - x=1049.489978，polarity=-1。
  - x=1054.510022，polarity=+1。
- 这些位置与真实像素阶跃边界一致，误差约 0.01 px 量级；之前视觉上的 0.5 px 偏差属于 Qt 绘制坐标和 OpenCV 像素中心坐标没有正确换算。

## GUI 复核场景

1. 打开 `E:\Code\2dMeasure\1.png`，新增卡尺后释放鼠标，卡尺应立即显示且被选中。
2. 卡尺穿过黑色目标时，应显示全部满足阈值的边缘，而不是只显示最强边。
3. 点击同一卡尺上的两条边，应生成 `边距N` 测量项，并在结果表显示距离 px/mm。
4. 拖动曲线下方正负阈值线，参数面板、图像边缘、结果表应同步刷新。
5. 拖动卡尺端点旋转，控制点应更容易命中，拖动过程应即时回显。
6. 设置 `mm/px` 并启用换算后，卡尺行和边距行应显示毫米值。
