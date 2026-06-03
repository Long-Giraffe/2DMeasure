# 卡尺软件修改报告

修改对象：Qt Widgets 配方测量软件与卡尺核心模型。

## 本次问题与修改

| 问题 | 原因 | 修改 |
|---|---|---|
| 只显示一条边缘 | UI 只绘制 `CaliperResult::selectedEdge()`，虽然核心已经输出 `edges` | `ImageView` 改为遍历并显示所有满足阈值的边缘，正负极性用不同颜色，选中卡尺更醒目 |
| 无法手动量两条边距离 | 配方模型只有卡尺，没有边对测量项 | 新增 `EdgePairMeasurement`，点击边 A 再点击同一卡尺边 B 生成测量项，并保存到 JSON |
| 边距测量不能随参数变化刷新 | 之前没有测量项重匹配逻辑 | 测量项保存边缘沿线位置，刷新时在当前检测边中匹配最近边；匹配失败显示 NG |
| 曲线在右侧，操作不直观 | 原布局把曲线和参数混在右侧 | 中间区域改为上方图像、下方曲线；右侧只保留参数和结果表 |
| 曲线缺刻度、阈值线不易拖 | 原曲线控件只画简单曲线和细虚线阈值 | 曲线增加 X/Y 刻度、网格、鼠标读数、粗阈值线、可拖动把手和悬停高亮 |
| sigma 默认值和步长不符合调参习惯 | 默认仍为 1.0，spinbox 未设置 0.1 步长 | `profileSmoothSigma`、`derivativeSigma` 默认改为 0.4；UI 步长改为 0.1；旧配方缺字段时按 0.4 |
| 新增卡尺释放鼠标后显示不及时 | 创建后先取消按钮状态，可能触发视图模式变化后再刷新 | 创建卡尺后先选中、检测、刷新视图，再阻塞信号取消“新增卡尺”按钮 |
| 拖端点旋转不灵敏 | 端点命中半径偏小，且拖动时依赖主窗口刷新后才回显 | 端点命中半径提高到 18 px，端点绘制变大；拖动时先本地更新 `ImageView` 再通知主窗口重算 |

## 配方与结果变化

- JSON 新增 `measurements` 数组，元素类型为 `edge_pair`。
- 单个边距测量项保存：`id/name/enabled/caliperToolId/edgeAPosition/edgeBPosition`。
- 旧配方没有 `measurements` 时正常加载。
- 结果表现在同时显示卡尺检测行和边距测量行。
- CSV 仍使用固定列：`image_path,tool_name,status,edge_count,x_px,y_px,position_px,value_mm,gradient,message`。
  - 卡尺行：`position_px` 为选中边沿线位置。
  - 边距行：`position_px` 为两条边距离，`value_mm` 为换算后的距离。

## 关键实现点

- `src/core/CaliperTypes.h`：新增 `EdgePairMeasurement`，sigma 默认值改为 0.4，`CaliperResult` 增加 profile 的 `xStart/xStep`。
- `src/core/Recipe.cpp`：保存/加载 `measurements[]`，旧配方 sigma 默认值改为 0.4。
- `src/ui/ImageView.cpp`：显示所有边缘、边缘点击选择、A/B 标记、端点大命中区和本地拖动回显。
- `src/ui/CurveWidget.cpp`：重写为带刻度、网格、阈值把手和鼠标读数的自绘曲线控件。
- `src/ui/MainWindow.cpp`：曲线移到图像下方，边缘点击生成边距测量项，结果表/CSV 输出测量项。

## 与 0.5 像素偏移的关系

- 核心检测仍使用 OpenCV 像素中心坐标，不再添加固定 `+0.5` 检测偏移。
- UI 绘制时保留图像坐标到 Qt 绘制坐标的 `+0.5` 显示换算，这是因为 QImage 绘制矩形的左上角是像素格边界，而检测点表示像素中心坐标。
- 因此：算法坐标不被移动；只有绘制 overlay 时做坐标系转换，避免视觉上差半个像素。
