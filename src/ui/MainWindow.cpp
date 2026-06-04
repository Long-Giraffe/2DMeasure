#include "ui/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace {

QString toQString(const std::string& value) {
    return QString::fromStdString(value);
}

std::string toStdString(const QString& value) {
    return value.toStdString();
}

QString csvQuote(QString value) {
    value.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QString polarityLabel(measure::EdgePolarity polarity) {
    switch (polarity) {
    case measure::EdgePolarity::DarkToBright:
        return QStringLiteral("黑到白");
    case measure::EdgePolarity::BrightToDark:
        return QStringLiteral("白到黑");
    case measure::EdgePolarity::Any:
    default:
        return QStringLiteral("任意");
    }
}

QString edgePickLabel(measure::EdgePickMode mode) {
    switch (mode) {
    case measure::EdgePickMode::First:
        return QStringLiteral("第一个");
    case measure::EdgePickMode::Last:
        return QStringLiteral("最后一个");
    case measure::EdgePickMode::Strongest:
    default:
        return QStringLiteral("最强");
    }
}

struct MeasurementResolve {
    bool ok = false;
    int toolAIndex = -1;
    int toolBIndex = -1;
    int edgeAIndex = -1;
    int edgeBIndex = -1;
    double distancePx = 0.0;
    QString message;
};

int nearestEdgeIndex(const measure::CaliperResult& result, double position, double maxDistance = 3.0) {
    int best = -1;
    double bestDistance = maxDistance;
    for (int i = 0; i < static_cast<int>(result.edges.size()); ++i) {
        const double distance = std::abs(result.edges[static_cast<size_t>(i)].position - position);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

MeasurementResolve resolveMeasurement(
    const measure::EdgePairMeasurement& measurement,
    const std::vector<measure::CaliperTool>& tools,
    const std::vector<measure::CaliperResult>& results) {
    MeasurementResolve resolved;
    if (!measurement.enabled) {
        resolved.message = QStringLiteral("Disabled");
        return resolved;
    }

    for (int i = 0; i < static_cast<int>(tools.size()); ++i) {
        if (tools[static_cast<size_t>(i)].id == measurement.toolAId) {
            resolved.toolAIndex = i;
        }
        if (tools[static_cast<size_t>(i)].id == measurement.toolBId) {
            resolved.toolBIndex = i;
        }
    }
    if (resolved.toolAIndex < 0 || resolved.toolBIndex < 0 ||
        resolved.toolAIndex >= static_cast<int>(results.size()) ||
        resolved.toolBIndex >= static_cast<int>(results.size())) {
        resolved.message = QStringLiteral("Caliper missing");
        return resolved;
    }

    const auto& resultA = results[static_cast<size_t>(resolved.toolAIndex)];
    const auto& resultB = results[static_cast<size_t>(resolved.toolBIndex)];
    resolved.edgeAIndex = nearestEdgeIndex(resultA, measurement.edgeAPosition);
    resolved.edgeBIndex = nearestEdgeIndex(resultB, measurement.edgeBPosition);
    if (resolved.edgeAIndex < 0 || resolved.edgeBIndex < 0) {
        resolved.message = QStringLiteral("Edge missing");
        return resolved;
    }
    if (resolved.toolAIndex == resolved.toolBIndex && resolved.edgeAIndex == resolved.edgeBIndex) {
        resolved.message = QStringLiteral("Same edge");
        return resolved;
    }

    const auto& edgeA = resultA.edges[static_cast<size_t>(resolved.edgeAIndex)];
    const auto& edgeB = resultB.edges[static_cast<size_t>(resolved.edgeBIndex)];
    resolved.distancePx = std::hypot(edgeB.point.x - edgeA.point.x, edgeB.point.y - edgeA.point.y);
    resolved.ok = true;
    resolved.message = QStringLiteral("OK");
    return resolved;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    buildUi();
    connectUi();
    refreshAll();
    setStatus(QStringLiteral("准备就绪"));
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("2D Measure - 卡尺配方测量"));
    resize(1500, 960);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralWidget"));
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    setCentralWidget(central);

    auto* mainSplitter = new QSplitter(Qt::Vertical, central);
    mainSplitter->setChildrenCollapsible(false);
    rootLayout->addWidget(mainSplitter);

    auto* workPanel = new QWidget(mainSplitter);
    auto* workLayout = new QVBoxLayout(workPanel);
    workLayout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, workPanel);
    workLayout->addWidget(splitter);

    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);

    auto* fileGroup = new QGroupBox(QStringLiteral("图像 / 配方"), leftPanel);
    auto* fileLayout = new QGridLayout(fileGroup);
    openImageButton_ = new QPushButton(QStringLiteral("打开图像"), fileGroup);
    loadRecipeButton_ = new QPushButton(QStringLiteral("加载配方"), fileGroup);
    saveRecipeButton_ = new QPushButton(QStringLiteral("保存配方"), fileGroup);
    exportCsvButton_ = new QPushButton(QStringLiteral("导出 CSV"), fileGroup);
    fileLayout->addWidget(openImageButton_, 0, 0);
    fileLayout->addWidget(loadRecipeButton_, 0, 1);
    fileLayout->addWidget(saveRecipeButton_, 1, 0);
    fileLayout->addWidget(exportCsvButton_, 1, 1);
    leftLayout->addWidget(fileGroup);

    auto* calibrationGroup = new QGroupBox(QStringLiteral("比例尺"), leftPanel);
    auto* calibrationLayout = new QFormLayout(calibrationGroup);
    calibrationEnabled_ = new QCheckBox(QStringLiteral("启用毫米换算"), calibrationGroup);
    mmPerPixelSpin_ = new QDoubleSpinBox(calibrationGroup);
    mmPerPixelSpin_->setRange(0.000001, 1000000.0);
    mmPerPixelSpin_->setDecimals(6);
    mmPerPixelSpin_->setValue(0.01);
    channelCombo_ = new QComboBox(calibrationGroup);
    channelCombo_->addItems({QStringLiteral("Gray"), QStringLiteral("Red"), QStringLiteral("Green"), QStringLiteral("Blue")});
    calibrationLayout->addRow(calibrationEnabled_);
    calibrationLayout->addRow(QStringLiteral("mm/px"), mmPerPixelSpin_);
    calibrationLayout->addRow(QStringLiteral("Channel"), channelCombo_);
    leftLayout->addWidget(calibrationGroup);

    auto* toolsGroup = new QGroupBox(QStringLiteral("卡尺列表"), leftPanel);
    auto* toolsLayout = new QVBoxLayout(toolsGroup);
    addCaliperButton_ = new QPushButton(QStringLiteral("新增卡尺"), toolsGroup);
    addCaliperButton_->setCheckable(true);
    addTemplateButton_ = new QPushButton(QStringLiteral("模板定位"), toolsGroup);
    addTemplateButton_->setCheckable(true);
    addCircleButton_ = new QPushButton(QStringLiteral("圆形卡尺"), toolsGroup);
    addCircleButton_->setCheckable(true);
    deleteCaliperButton_ = new QPushButton(QStringLiteral("删除选中卡尺"), toolsGroup);
    toolList_ = new QListWidget(toolsGroup);
    auto* createToolLayout = new QHBoxLayout();
    createToolLayout->setSpacing(5);
    createToolLayout->addWidget(addCaliperButton_);
    createToolLayout->addWidget(addTemplateButton_);
    createToolLayout->addWidget(addCircleButton_);
    toolsLayout->addLayout(createToolLayout);
    toolsLayout->addWidget(deleteCaliperButton_);
    toolsLayout->addWidget(toolList_, 1);
    leftLayout->addWidget(toolsGroup, 1);
    splitter->addWidget(leftPanel);

    auto* centerPanel = new QWidget(splitter);
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(6);

    auto* imageToolbar = new QWidget(centerPanel);
    imageToolbar->setObjectName(QStringLiteral("imageToolbar"));
    auto* imageToolbarLayout = new QHBoxLayout(imageToolbar);
    imageToolbarLayout->setContentsMargins(8, 4, 8, 4);
    imageToolbarLayout->setSpacing(6);
    auto* imageToolbarTitle = new QLabel(QStringLiteral("图像视图"), imageToolbar);
    imageToolbarTitle->setObjectName(QStringLiteral("imageToolbarTitle"));
    fitImageButton_ = new QPushButton(QStringLiteral("适应窗口"), imageToolbar);
    fitImageButton_->setObjectName(QStringLiteral("compactButton"));
    fitImageButton_->setToolTip(QStringLiteral("将图像恢复为适应窗口的显示比例"));
    imageToolbarLayout->addWidget(imageToolbarTitle);
    imageToolbarLayout->addStretch(1);
    imageToolbarLayout->addWidget(fitImageButton_);
    centerLayout->addWidget(imageToolbar);

    imageView_ = new ImageView(centerPanel);
    centerLayout->addWidget(imageView_, 1);

    curveGroup_ = new QGroupBox(QStringLiteral("灰度 / 导数曲线"), centerPanel);
    auto* centerCurveLayout = new QVBoxLayout(curveGroup_);
    auto* centerCurveModeCombo = new QComboBox(curveGroup_);
    centerCurveModeCombo->addItems({QStringLiteral("灰度+导数"), QStringLiteral("灰度"), QStringLiteral("导数")});
    auto* centerCurveWidget = new CurveWidget(curveGroup_);
    centerCurveLayout->addWidget(centerCurveModeCombo);
    centerCurveLayout->addWidget(centerCurveWidget, 1);
    centerLayout->addWidget(curveGroup_, 0);
    splitter->addWidget(centerPanel);

    auto* rightPanel = new QWidget(splitter);
    rightPanel->setMinimumWidth(300);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    auto* paramScroll = new QScrollArea(rightPanel);
    paramScroll->setWidgetResizable(true);
    paramScroll->setFrameShape(QFrame::NoFrame);

    auto* paramGroup = new QGroupBox(QStringLiteral("卡尺参数"), rightPanel);
    auto* paramLayout = new QFormLayout(paramGroup);
    nameEdit_ = new QLineEdit(paramGroup);
    widthSpin_ = new QSpinBox(paramGroup);
    widthSpin_->setRange(1, 1000);
    profileSigmaSpin_ = new QDoubleSpinBox(paramGroup);
    profileSigmaSpin_->setRange(0.0, 100.0);
    profileSigmaSpin_->setDecimals(1);
    profileSigmaSpin_->setSingleStep(0.1);
    derivativeSigmaSpin_ = new QDoubleSpinBox(paramGroup);
    derivativeSigmaSpin_->setRange(0.1, 100.0);
    derivativeSigmaSpin_->setDecimals(1);
    derivativeSigmaSpin_->setSingleStep(0.1);
    positiveThresholdSpin_ = new QDoubleSpinBox(paramGroup);
    positiveThresholdSpin_->setRange(0.0, 100000.0);
    positiveThresholdSpin_->setDecimals(3);
    negativeThresholdSpin_ = new QDoubleSpinBox(paramGroup);
    negativeThresholdSpin_->setRange(-100000.0, 0.0);
    negativeThresholdSpin_->setDecimals(3);
    polarityCombo_ = new QComboBox(paramGroup);
    polarityCombo_->addItems({QStringLiteral("任意"), QStringLiteral("黑到白"), QStringLiteral("白到黑")});
    edgePickCombo_ = new QComboBox(paramGroup);
    edgePickCombo_->addItems({QStringLiteral("最强"), QStringLiteral("第一个"), QStringLiteral("最后一个")});
    centerXSpin_ = new QDoubleSpinBox(paramGroup);
    centerYSpin_ = new QDoubleSpinBox(paramGroup);
    innerRadiusSpin_ = new QDoubleSpinBox(paramGroup);
    outerRadiusSpin_ = new QDoubleSpinBox(paramGroup);
    for (auto* spin : {centerXSpin_, centerYSpin_, innerRadiusSpin_, outerRadiusSpin_}) {
        spin->setRange(-1000000.0, 1000000.0);
        spin->setDecimals(3);
    }
    innerRadiusSpin_->setRange(0.0, 1000000.0);
    outerRadiusSpin_->setRange(0.0, 1000000.0);
    sampleCountSpin_ = new QSpinBox(paramGroup);
    sampleCountSpin_->setRange(8, 1440);
    circleFitCombo_ = new QComboBox(paramGroup);
    circleFitCombo_->addItems({QStringLiteral("最小二乘"), QStringLiteral("RANSAC")});
    ransacThresholdSpin_ = new QDoubleSpinBox(paramGroup);
    ransacThresholdSpin_->setRange(0.01, 1000.0);
    ransacThresholdSpin_->setDecimals(3);
    ransacIterationsSpin_ = new QSpinBox(paramGroup);
    ransacIterationsSpin_->setRange(1, 10000);
    templateThresholdSpin_ = new QDoubleSpinBox(paramGroup);
    templateThresholdSpin_->setRange(0.0, 1.0);
    templateThresholdSpin_->setDecimals(3);
    templateThresholdSpin_->setSingleStep(0.05);

    paramLayout->addRow(QStringLiteral("名称"), nameEdit_);

    toolParamStack_ = new QStackedWidget(paramGroup);
    lineParamPage_ = new QWidget(toolParamStack_);
    auto* lineLayout = new QFormLayout(lineParamPage_);
    lineLayout->addRow(QStringLiteral("宽度"), widthSpin_);
    toolParamStack_->addWidget(lineParamPage_);

    circleParamPage_ = new QWidget(toolParamStack_);
    auto* circleLayout = new QFormLayout(circleParamPage_);
    circleLayout->addRow(QStringLiteral("圆心 X"), centerXSpin_);
    circleLayout->addRow(QStringLiteral("圆心 Y"), centerYSpin_);
    circleLayout->addRow(QStringLiteral("内半径"), innerRadiusSpin_);
    circleLayout->addRow(QStringLiteral("外半径"), outerRadiusSpin_);
    circleLayout->addRow(QStringLiteral("径向卡尺数"), sampleCountSpin_);
    circleLayout->addRow(QStringLiteral("圆拟合"), circleFitCombo_);
    toolParamStack_->addWidget(circleParamPage_);

    templateParamPage_ = new QWidget(toolParamStack_);
    auto* templateLayout = new QFormLayout(templateParamPage_);
    templateLayout->addRow(QStringLiteral("模板最低分"), templateThresholdSpin_);
    toolParamStack_->addWidget(templateParamPage_);
    paramLayout->addRow(toolParamStack_);

    edgeParamGroup_ = new QGroupBox(QStringLiteral("边缘参数"), paramGroup);
    auto* edgeLayout = new QFormLayout(edgeParamGroup_);
    edgeLayout->addRow(QStringLiteral("极性"), polarityCombo_);
    edgeLayout->addRow(QStringLiteral("边缘选择"), edgePickCombo_);
    paramLayout->addRow(edgeParamGroup_);

    advancedParamGroup_ = new QGroupBox(QStringLiteral("高级参数"), paramGroup);
    advancedParamGroup_->setCheckable(true);
    advancedParamGroup_->setChecked(false);
    auto* advancedLayout = new QFormLayout(advancedParamGroup_);
    advancedLayout->addRow(QStringLiteral("灰度平滑 σ"), profileSigmaSpin_);
    advancedLayout->addRow(QStringLiteral("导数 σ"), derivativeSigmaSpin_);
    advancedLayout->addRow(QStringLiteral("正阈值"), positiveThresholdSpin_);
    advancedLayout->addRow(QStringLiteral("负阈值"), negativeThresholdSpin_);
    advancedLayout->addRow(QStringLiteral("RANSAC阈值"), ransacThresholdSpin_);
    advancedLayout->addRow(QStringLiteral("RANSAC次数"), ransacIterationsSpin_);
    profileSigmaSpin_->setVisible(false);
    derivativeSigmaSpin_->setVisible(false);
    positiveThresholdSpin_->setVisible(false);
    negativeThresholdSpin_->setVisible(false);
    ransacThresholdSpin_->setVisible(false);
    ransacIterationsSpin_->setVisible(false);
    for (int row = 0; row < advancedLayout->rowCount(); ++row) {
        if (auto* item = advancedLayout->itemAt(row, QFormLayout::LabelRole)) {
            item->widget()->setVisible(false);
        }
    }
    connect(advancedParamGroup_, &QGroupBox::toggled, this, [=](bool checked) {
        const bool circleAdvanced = currentTool() && currentTool()->type == measure::ToolType::CircleCaliper;
        profileSigmaSpin_->setVisible(checked);
        derivativeSigmaSpin_->setVisible(checked);
        positiveThresholdSpin_->setVisible(checked);
        negativeThresholdSpin_->setVisible(checked);
        ransacThresholdSpin_->setVisible(checked && circleAdvanced);
        ransacIterationsSpin_->setVisible(checked && circleAdvanced);
        for (int row = 0; row < advancedLayout->rowCount(); ++row) {
            if (auto* item = advancedLayout->itemAt(row, QFormLayout::LabelRole)) {
                item->widget()->setVisible(checked && (row < 4 || circleAdvanced));
            }
        }
    });
    paramLayout->addRow(advancedParamGroup_);
    paramScroll->setWidget(paramGroup);
    rightLayout->addWidget(paramScroll, 1);

    curveModeCombo_ = centerCurveModeCombo;
    curveWidget_ = centerCurveWidget;

    splitter->addWidget(rightPanel);

    auto* resultGroup = new QGroupBox(QStringLiteral("测量结果"), mainSplitter);
    resultGroup->setMinimumHeight(300);
    auto* resultLayout = new QVBoxLayout(resultGroup);
    resultLayout->setContentsMargins(8, 8, 8, 8);

    auto* resultTabs = new QTabWidget(resultGroup);
    resultLayout->addWidget(resultTabs, 1);
    auto* currentResultPage = new QWidget(resultTabs);
    auto* currentResultLayout = new QVBoxLayout(currentResultPage);
    currentResultLayout->setContentsMargins(0, 0, 0, 0);

    resultTable_ = new QTableWidget(currentResultPage);
    resultTable_->setColumnCount(9);
    resultTable_->setHorizontalHeaderLabels({
        QStringLiteral("名称"),
        QStringLiteral("类型"),
        QStringLiteral("状态"),
        QStringLiteral("边数"),
        QStringLiteral("X/A(px)"),
        QStringLiteral("Y/B(px)"),
        QStringLiteral("位置/距离(px)"),
        QStringLiteral("值(mm)"),
        QStringLiteral("梯度/信息")
    });
    resultTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    resultTable_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    resultTable_->setAlternatingRowColors(true);
    resultTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    resultTable_->horizontalHeader()->setStretchLastSection(true);
    resultTable_->setColumnWidth(0, 180);
    resultTable_->setColumnWidth(1, 90);
    resultTable_->setColumnWidth(2, 90);
    resultTable_->setColumnWidth(3, 70);
    resultTable_->setColumnWidth(4, 100);
    resultTable_->setColumnWidth(5, 100);
    resultTable_->setColumnWidth(6, 150);
    resultTable_->setColumnWidth(7, 120);
    resultTable_->setColumnWidth(8, 220);
    resultTable_->verticalHeader()->setVisible(false);
    currentResultLayout->addWidget(resultTable_, 1);
    resultTabs->addTab(currentResultPage, QStringLiteral("当前结果"));

    auto* batchPage = new QWidget(resultTabs);
    auto* batchLayout = new QVBoxLayout(batchPage);
    auto* batchControls = new QHBoxLayout();
    selectBatchFolderButton_ = new QPushButton(QStringLiteral("选择文件夹"), batchPage);
    startBatchButton_ = new QPushButton(QStringLiteral("开始"), batchPage);
    pauseBatchButton_ = new QPushButton(QStringLiteral("暂停"), batchPage);
    cancelBatchButton_ = new QPushButton(QStringLiteral("取消"), batchPage);
    exportBatchCsvButton_ = new QPushButton(QStringLiteral("导出汇总 CSV"), batchPage);
    exportBatchCsvButton_->setEnabled(false);
    batchNgModeCombo_ = new QComboBox(batchPage);
    batchNgModeCombo_->addItems({QStringLiteral("记录NG继续"), QStringLiteral("NG暂停")});
    batchProgress_ = new QProgressBar(batchPage);
    batchControls->addWidget(selectBatchFolderButton_);
    batchControls->addWidget(startBatchButton_);
    batchControls->addWidget(pauseBatchButton_);
    batchControls->addWidget(cancelBatchButton_);
    batchControls->addWidget(exportBatchCsvButton_);
    batchControls->addWidget(batchNgModeCombo_);
    batchControls->addWidget(batchProgress_, 1);
    batchLayout->addLayout(batchControls);
    batchTable_ = new QTableWidget(batchPage);
    batchTable_->setColumnCount(7);
    batchTable_->setHorizontalHeaderLabels({
        QStringLiteral("图像"), QStringLiteral("名称"), QStringLiteral("类型"),
        QStringLiteral("状态"), QStringLiteral("数值"), QStringLiteral("单位"), QStringLiteral("信息")});
    batchTable_->horizontalHeader()->setStretchLastSection(true);
    batchTable_->verticalHeader()->setVisible(false);
    batchTable_->setAlternatingRowColors(true);
    batchTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    batchTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    batchLayout->addWidget(batchTable_, 1);
    resultTabs->addTab(batchPage, QStringLiteral("批量测试"));
    batchTimer_ = new QTimer(this);
    batchTimer_->setSingleShot(true);

    openImageButton_->setProperty("role", "primary");
    startBatchButton_->setProperty("role", "primary");
    deleteCaliperButton_->setProperty("role", "danger");
    cancelBatchButton_->setProperty("role", "danger");
    exportBatchCsvButton_->setProperty("role", "primary");
    addCaliperButton_->setProperty("role", "tool");
    addTemplateButton_->setProperty("role", "tool");
    addCircleButton_->setProperty("role", "tool");

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({280, 880, 340});
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({580, 360});
}

void MainWindow::connectUi() {
    connect(openImageButton_, &QPushButton::clicked, this, &MainWindow::openImage);
    connect(saveRecipeButton_, &QPushButton::clicked, this, &MainWindow::saveRecipe);
    connect(loadRecipeButton_, &QPushButton::clicked, this, &MainWindow::loadRecipe);
    connect(exportCsvButton_, &QPushButton::clicked, this, &MainWindow::exportCsv);
    connect(selectBatchFolderButton_, &QPushButton::clicked, this, &MainWindow::selectBatchFolder);
    connect(startBatchButton_, &QPushButton::clicked, this, &MainWindow::startBatch);
    connect(pauseBatchButton_, &QPushButton::clicked, this, &MainWindow::toggleBatchPause);
    connect(cancelBatchButton_, &QPushButton::clicked, this, &MainWindow::cancelBatch);
    connect(exportBatchCsvButton_, &QPushButton::clicked, this, &MainWindow::exportBatchCsvAs);
    connect(fitImageButton_, &QPushButton::clicked, imageView_, &ImageView::resetView);
    connect(batchTimer_, &QTimer::timeout, this, &MainWindow::processNextBatchImage);
    connect(batchTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= static_cast<int>(batchRows_.size())) {
            return;
        }
        QFile file(batchRows_[static_cast<size_t>(row)].imagePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        const QByteArray bytes = file.readAll();
        const std::vector<uchar> buffer(bytes.begin(), bytes.end());
        const cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
        if (decoded.empty()) {
            return;
        }
        colorBgr_ = decoded;
        cv::cvtColor(colorBgr_, gray_, cv::COLOR_BGR2GRAY);
        std::vector<cv::Mat> channels;
        cv::split(colorBgr_, channels);
        blue_ = channels[0];
        green_ = channels[1];
        red_ = channels[2];
        recomputeAll();
        refreshAll(true);
    });
    connect(deleteCaliperButton_, &QPushButton::clicked, this, &MainWindow::deleteCurrentCaliper);
    connect(addCaliperButton_, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            addTemplateButton_->setChecked(false);
            addCircleButton_->setChecked(false);
            imageView_->setCreateMode(ImageView::CreateMode::LineCaliper);
        } else if (!addTemplateButton_->isChecked() && !addCircleButton_->isChecked()) {
            imageView_->setCreateMode(ImageView::CreateMode::None);
        }
    });
    connect(addTemplateButton_, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            addCaliperButton_->setChecked(false);
            addCircleButton_->setChecked(false);
            imageView_->setCreateMode(ImageView::CreateMode::TemplateLocator);
        } else if (!addCaliperButton_->isChecked() && !addCircleButton_->isChecked()) {
            imageView_->setCreateMode(ImageView::CreateMode::None);
        }
    });
    connect(addCircleButton_, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            addCaliperButton_->setChecked(false);
            addTemplateButton_->setChecked(false);
            imageView_->setCreateMode(ImageView::CreateMode::CircleCaliper);
        } else if (!addCaliperButton_->isChecked() && !addTemplateButton_->isChecked()) {
            imageView_->setCreateMode(ImageView::CreateMode::None);
        }
    });
    connect(imageView_, &ImageView::caliperCreated, this, &MainWindow::addCaliper);
    connect(imageView_, &ImageView::templateCreated, this, &MainWindow::addTemplateLocator);
    connect(imageView_, &ImageView::circleCaliperCreated, this, &MainWindow::addCircleCaliper);
    connect(imageView_, &ImageView::selectedToolChanged, this, &MainWindow::selectTool);
    connect(imageView_, &ImageView::toolGeometryChanged, this, &MainWindow::updateToolGeometry);
    connect(imageView_, &ImageView::circleGeometryChanged, this, &MainWindow::updateCircleGeometry);
    connect(imageView_, &ImageView::templateGeometryChanged, this, &MainWindow::updateTemplateGeometry);
    connect(imageView_, &ImageView::edgeClicked, this, &MainWindow::handleEdgeClicked);

    connect(toolList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (updatingUi_ || row < 0 || row >= static_cast<int>(recipe_.tools.size())) {
            return;
        }
        selectedToolId_ = recipe_.tools[static_cast<size_t>(row)].id;
        refreshAll();
    });

    auto parameterChanged = [this]() { applyParametersToCurrentTool(); };
    connect(nameEdit_, &QLineEdit::textChanged, this, parameterChanged);
    connect(widthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, parameterChanged);
    connect(profileSigmaSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(derivativeSigmaSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(positiveThresholdSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(negativeThresholdSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(polarityCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, parameterChanged);
    connect(edgePickCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, parameterChanged);
    connect(centerXSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(centerYSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(innerRadiusSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(outerRadiusSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(sampleCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, parameterChanged);
    connect(circleFitCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, parameterChanged);
    connect(ransacThresholdSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);
    connect(ransacIterationsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, parameterChanged);
    connect(templateThresholdSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, parameterChanged);

    connect(curveWidget_, &CurveWidget::thresholdsChanged, this, &MainWindow::applyThresholdsFromCurve);
    connect(curveModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index == 1) {
            curveWidget_->setDisplayMode(CurveWidget::DisplayMode::Profile);
        } else if (index == 2) {
            curveWidget_->setDisplayMode(CurveWidget::DisplayMode::Gradient);
        } else {
            curveWidget_->setDisplayMode(CurveWidget::DisplayMode::Both);
        }
    });

    connect(calibrationEnabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        recipe_.calibration.enabled = enabled;
        refreshAll();
    });
    connect(mmPerPixelSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        recipe_.calibration.mmPerPixel = value;
        refreshAll();
    });
    connect(channelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        recipe_.defaultChannel = index == 1 ? measure::ImageChannel::Red :
                                 index == 2 ? measure::ImageChannel::Green :
                                 index == 3 ? measure::ImageChannel::Blue :
                                              measure::ImageChannel::Gray;
        recomputeAll();
        refreshAll();
    });
}

bool MainWindow::loadImage(const QString& path, QString* errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取图像文件：") + file.errorString();
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    std::vector<uchar> buffer(bytes.begin(), bytes.end());
    cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
    if (decoded.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OpenCV 无法解码该图像");
        }
        return false;
    }

    colorBgr_ = decoded;
    cv::cvtColor(colorBgr_, gray_, cv::COLOR_BGR2GRAY);
    std::vector<cv::Mat> channels;
    cv::split(colorBgr_, channels);
    blue_ = channels[0];
    green_ = channels[1];
    red_ = channels[2];
    image_ = currentPreviewImage();
    recipe_.imagePath = toStdString(path);
    recomputeAll();
    refreshAll(true);
    setStatus(QStringLiteral("已加载图像：") + path);
    return true;
}

void MainWindow::openImage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开图像"),
        QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff);;All Files (*.*)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!loadImage(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), error);
    }
}

void MainWindow::saveRecipe() {
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("保存配方"),
        QString(),
        QStringLiteral("Recipe (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!measure::RecipeCodec::saveToFile(recipe_, path, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    setStatus(QStringLiteral("已保存配方：") + path);
}

void MainWindow::loadRecipe() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("加载配方"),
        QString(),
        QStringLiteral("Recipe (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    measure::Recipe loaded;
    QString error;
    if (!measure::RecipeCodec::loadFromFile(path, &loaded, &error)) {
        QMessageBox::warning(this, QStringLiteral("加载失败"), error);
        return;
    }

    recipe_ = loaded;
    selectedToolId_ = recipe_.tools.empty() ? std::string() : recipe_.tools.front().id;
    hasPendingEdge_ = false;
    pendingEdgeToolId_.clear();
    pendingEdgePosition_ = 0.0;

    if (!recipe_.imagePath.empty()) {
        QString imageError;
        if (!loadImage(toQString(recipe_.imagePath), &imageError)) {
            gray_.release();
            image_ = QImage();
            QMessageBox::information(
                this,
                QStringLiteral("图像未加载"),
                QStringLiteral("配方已加载，但图像路径不可用：\n") + imageError);
        }
    }

    recomputeAll();
    refreshAll();
    setStatus(QStringLiteral("已加载配方：") + path);
}

void MainWindow::exportCsv() {
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出测量结果"),
        QString(),
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), file.errorString());
        return;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "image_path,tool_name,status,edge_count,x_px,y_px,position_px,value_mm,gradient,message\n";
    for (size_t i = 0; i < recipe_.tools.size(); ++i) {
        const auto& tool = recipe_.tools[i];
        const auto& result = i < results_.size() ? results_[i] : measure::CaliperResult();
        const auto* edge = result.selectedEdge();
        const QString valueMm = (edge && recipe_.calibration.enabled)
            ? QString::number(edge->position * recipe_.calibration.mmPerPixel, 'f', 6)
            : QString();
        out << '"' << toQString(recipe_.imagePath) << '"' << ','
            << '"' << toQString(tool.name) << '"' << ','
            << (result.ok ? "OK" : "NG") << ','
            << result.edges.size() << ','
            << (edge ? QString::number(edge->point.x, 'f', 4) : QString()) << ','
            << (edge ? QString::number(edge->point.y, 'f', 4) : QString()) << ','
            << (edge ? QString::number(edge->position, 'f', 4) : QString()) << ','
            << valueMm << ','
            << (edge ? QString::number(edge->gradient, 'f', 4) : QString()) << ','
            << '"' << toQString(result.message) << '"' << '\n';
    }
    for (const auto& measurement : recipe_.measurements) {
        const MeasurementResolve resolved = resolveMeasurement(measurement, recipe_.tools, results_);
        QString xPx;
        QString yPx;
        QString message = resolved.message;
        if (resolved.ok) {
            const auto& resultA = results_[static_cast<size_t>(resolved.toolAIndex)];
            const auto& resultB = results_[static_cast<size_t>(resolved.toolBIndex)];
            const auto& edgeA = resultA.edges[static_cast<size_t>(resolved.edgeAIndex)];
            const auto& edgeB = resultB.edges[static_cast<size_t>(resolved.edgeBIndex)];
            xPx = QString::number(edgeA.point.x, 'f', 4);
            yPx = QString::number(edgeA.point.y, 'f', 4);
            message = QStringLiteral("A(%1,%2) B(%3,%4)")
                .arg(QString::number(edgeA.point.x, 'f', 3))
                .arg(QString::number(edgeA.point.y, 'f', 3))
                .arg(QString::number(edgeB.point.x, 'f', 3))
                .arg(QString::number(edgeB.point.y, 'f', 3));
        }
        const QString valueMm = (resolved.ok && recipe_.calibration.enabled)
            ? QString::number(resolved.distancePx * recipe_.calibration.mmPerPixel, 'f', 6)
            : QString();
        out << '"' << toQString(recipe_.imagePath) << '"' << ','
            << '"' << toQString(measurement.name) << '"' << ','
            << (resolved.ok ? "OK" : "NG") << ','
            << (resolved.ok ? 2 : 0) << ','
            << xPx << ','
            << yPx << ','
            << (resolved.ok ? QString::number(resolved.distancePx, 'f', 4) : QString()) << ','
            << valueMm << ','
            << QString() << ','
            << '"' << message << '"' << '\n';
    }
    setStatus(QStringLiteral("已导出 CSV：") + path);
}

void MainWindow::selectBatchFolder() {
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("选择批量测试文件夹"), batchFolder_);
    if (folder.isEmpty()) {
        return;
    }
    batchFolder_ = folder;
    setStatus(QStringLiteral("批量文件夹：") + folder);
}

void MainWindow::startBatch() {
    if (batchFolder_.isEmpty()) {
        selectBatchFolder();
    }
    if (batchFolder_.isEmpty()) {
        return;
    }

    QDir dir(batchFolder_);
    batchFiles_ = dir.entryList(
        {QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.bmp"),
         QStringLiteral("*.tif"), QStringLiteral("*.tiff")},
        QDir::Files,
        QDir::Name);
    for (QString& file : batchFiles_) {
        file = dir.absoluteFilePath(file);
    }
    if (batchFiles_.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("批量测试"), QStringLiteral("文件夹中没有支持的图像文件"));
        return;
    }

    batchRows_.clear();
    batchTable_->setRowCount(0);
    exportBatchCsvButton_->setEnabled(false);
    batchIndex_ = 0;
    batchRunning_ = true;
    batchPaused_ = false;
    batchCancelRequested_ = false;
    batchProgress_->setRange(0, batchFiles_.size());
    batchProgress_->setValue(0);
    pauseBatchButton_->setText(QStringLiteral("暂停"));
    batchTimer_->start(0);
}

void MainWindow::toggleBatchPause() {
    if (!batchRunning_) {
        return;
    }
    batchPaused_ = !batchPaused_;
    pauseBatchButton_->setText(batchPaused_ ? QStringLiteral("继续") : QStringLiteral("暂停"));
    if (!batchPaused_) {
        batchTimer_->start(0);
    }
}

void MainWindow::cancelBatch() {
    batchCancelRequested_ = true;
    batchPaused_ = false;
    if (batchRunning_) {
        batchTimer_->start(0);
    }
}

void MainWindow::processNextBatchImage() {
    if (!batchRunning_ || batchPaused_) {
        return;
    }
    if (batchCancelRequested_ || batchIndex_ >= batchFiles_.size()) {
        batchRunning_ = false;
        exportBatchCsv();
        QString statusText = batchCancelRequested_ ? QStringLiteral("批量测试已取消") : QStringLiteral("批量测试完成");
        if (!batchFolder_.isEmpty() && !batchRows_.empty()) {
            statusText += QStringLiteral("；汇总 CSV：") +
                QDir(batchFolder_).absoluteFilePath(QStringLiteral("batch_results.csv"));
        }
        setStatus(statusText);
        return;
    }

    const QString imagePath = batchFiles_[batchIndex_];
    bool imageNg = false;
    QFile file(imagePath);
    cv::Mat decoded;
    QString loadMessage;
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = file.readAll();
        const std::vector<uchar> buffer(bytes.begin(), bytes.end());
        decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
    }
    if (decoded.empty()) {
        loadMessage = QStringLiteral("图像加载失败");
        imageNg = true;
        batchRows_.push_back({imagePath, QStringLiteral("图像"), QStringLiteral("加载"), QStringLiteral("NG"), QString(), QString(), loadMessage});
    } else {
        const measure::RecipeRunResult run = runner_.run(recipe_, decoded);
        for (int i = 0; i < static_cast<int>(recipe_.tools.size()); ++i) {
            const auto& tool = recipe_.tools[static_cast<size_t>(i)];
            const auto& result = run.toolResults[static_cast<size_t>(i)];
            BatchRow row;
            row.imagePath = imagePath;
            row.name = toQString(tool.name);
            row.type = tool.type == measure::ToolType::TemplateLocator ? QStringLiteral("模板") :
                       tool.type == measure::ToolType::CircleCaliper ? QStringLiteral("圆卡尺") : QStringLiteral("线卡尺");
            row.status = result.ok ? QStringLiteral("OK") : QStringLiteral("NG");
            imageNg = imageNg || !result.ok;
            if (tool.type == measure::ToolType::TemplateLocator) {
                row.value = QString::number(result.matchScore, 'f', 4);
                row.unit = QStringLiteral("score");
            } else if (tool.type == measure::ToolType::CircleCaliper) {
                row.value = result.ok ? QString::number(result.fittedDiameter, 'f', 4) : QString();
                row.unit = QStringLiteral("px");
            } else if (const auto* edge = result.selectedEdge()) {
                row.value = QString::number(edge->position, 'f', 4);
                row.unit = QStringLiteral("px");
            }
            row.message = toQString(result.message);
            batchRows_.push_back(row);
        }
        for (int i = 0; i < static_cast<int>(recipe_.measurements.size()); ++i) {
            const auto& measurement = recipe_.measurements[static_cast<size_t>(i)];
            const auto& result = run.measurementResults[static_cast<size_t>(i)];
            batchRows_.push_back({
                imagePath,
                toQString(measurement.name),
                QStringLiteral("边距"),
                result.ok ? QStringLiteral("OK") : QStringLiteral("NG"),
                result.ok ? QString::number(result.distancePx, 'f', 4) : QString(),
                QStringLiteral("px"),
                toQString(result.message)});
            imageNg = imageNg || !result.ok;
        }
    }

    batchTable_->setRowCount(static_cast<int>(batchRows_.size()));
    for (int row = 0; row < static_cast<int>(batchRows_.size()); ++row) {
        const auto& item = batchRows_[static_cast<size_t>(row)];
        const QString values[] = {
            item.imagePath, item.name, item.type, item.status, item.value, item.unit, item.message};
        for (int col = 0; col < 7; ++col) {
            batchTable_->setItem(row, col, new QTableWidgetItem(values[col]));
        }
    }
    exportBatchCsvButton_->setEnabled(!batchRows_.empty());

    ++batchIndex_;
    batchProgress_->setValue(batchIndex_);
    if (imageNg && batchNgModeCombo_->currentIndex() == 1) {
        batchPaused_ = true;
        pauseBatchButton_->setText(QStringLiteral("继续"));
        setStatus(QStringLiteral("批量测试遇到 NG，已暂停：") + imagePath);
        return;
    }
    batchTimer_->start(0);
}

void MainWindow::exportBatchCsv() {
    if (batchFolder_.isEmpty() || batchRows_.empty()) {
        return;
    }
    const QString path = QDir(batchFolder_).absoluteFilePath(QStringLiteral("batch_results.csv"));
    QString error;
    if (!writeBatchCsv(path, &error)) {
        setStatus(QStringLiteral("批量汇总 CSV 导出失败：") + error);
    }
}

void MainWindow::exportBatchCsvAs() {
    if (batchRows_.empty()) {
        QMessageBox::information(this, QStringLiteral("导出批量结果"), QStringLiteral("当前没有可导出的批量测量结果。"));
        return;
    }

    const QString defaultPath = batchFolder_.isEmpty()
        ? QStringLiteral("batch_results.csv")
        : QDir(batchFolder_).absoluteFilePath(QStringLiteral("batch_results.csv"));
    const QString path = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出批量测量汇总"),
        defaultPath,
        QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!writeBatchCsv(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    setStatus(QStringLiteral("已导出批量测量汇总：") + path);
}

bool MainWindow::writeBatchCsv(const QString& path, QString* errorMessage) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "image_path,name,type,status,value,unit,message\n";
    for (const auto& row : batchRows_) {
        out << csvQuote(row.imagePath) << ','
            << csvQuote(row.name) << ','
            << csvQuote(row.type) << ','
            << csvQuote(row.status) << ','
            << csvQuote(row.value) << ','
            << csvQuote(row.unit) << ','
            << csvQuote(row.message) << '\n';
    }
    return true;
}

void MainWindow::addCaliper(QPointF p1, QPointF p2) {
    const cv::Point2d locatorOffset = currentLocatorOffset();
    measure::CaliperTool tool;
    tool.id = toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    tool.name = toStdString(makeDefaultToolName());
    tool.type = measure::ToolType::LineCaliper;
    tool.channel = recipe_.defaultChannel;
    tool.p1 = {p1.x() - locatorOffset.x, p1.y() - locatorOffset.y};
    tool.p2 = {p2.x() - locatorOffset.x, p2.y() - locatorOffset.y};
    recipe_.tools.push_back(tool);
    selectedToolId_ = tool.id;
    recomputeAll();
    refreshAll();
    {
        const QSignalBlocker blocker(addCaliperButton_);
        addCaliperButton_->setChecked(false);
    }
    imageView_->setCreateMode(false);
}

void MainWindow::addTemplateLocator(QRectF roi) {
    const cv::Mat& source = currentMeasurementImage();
    if (source.empty()) {
        return;
    }

    const int x = std::max(0, static_cast<int>(std::floor(roi.x())));
    const int y = std::max(0, static_cast<int>(std::floor(roi.y())));
    const int right = std::min(source.cols, static_cast<int>(std::ceil(roi.right())));
    const int bottom = std::min(source.rows, static_cast<int>(std::ceil(roi.bottom())));
    if (right <= x || bottom <= y) {
        return;
    }

    std::vector<uchar> png;
    cv::imencode(".png", source(cv::Rect(x, y, right - x, bottom - y)), png);
    const QByteArray bytes(reinterpret_cast<const char*>(png.data()), static_cast<int>(png.size()));

    measure::CaliperTool tool;
    tool.id = toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    tool.name = "Template locator";
    tool.type = measure::ToolType::TemplateLocator;
    tool.channel = recipe_.defaultChannel;
    tool.templateRoi = cv::Rect2d(x, y, right - x, bottom - y);
    const double marginX = tool.templateRoi.width;
    const double marginY = tool.templateRoi.height;
    const double searchX = std::max(0.0, tool.templateRoi.x - marginX);
    const double searchY = std::max(0.0, tool.templateRoi.y - marginY);
    const double searchRight = std::min(static_cast<double>(source.cols), tool.templateRoi.x + tool.templateRoi.width + marginX);
    const double searchBottom = std::min(static_cast<double>(source.rows), tool.templateRoi.y + tool.templateRoi.height + marginY);
    tool.searchRoi = cv::Rect2d(searchX, searchY, searchRight - searchX, searchBottom - searchY);
    tool.templateReferencePoint = {tool.templateRoi.x, tool.templateRoi.y};
    tool.templateImageBase64Png = bytes.toBase64().toStdString();
    recipe_.tools.push_back(tool);
    selectedToolId_ = tool.id;
    recomputeAll();
    refreshAll();
    {
        const QSignalBlocker blocker(addTemplateButton_);
        addTemplateButton_->setChecked(false);
    }
    imageView_->setCreateMode(false);
}

void MainWindow::addCircleCaliper(QPointF center, double innerRadius, double outerRadius) {
    const cv::Point2d locatorOffset = currentLocatorOffset();
    measure::CaliperTool tool;
    tool.id = toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    tool.name = QStringLiteral("圆形卡尺%1").arg(recipe_.tools.size() + 1).toStdString();
    tool.type = measure::ToolType::CircleCaliper;
    tool.channel = recipe_.defaultChannel;
    tool.center = {center.x() - locatorOffset.x, center.y() - locatorOffset.y};
    tool.innerRadius = innerRadius;
    tool.outerRadius = outerRadius;
    recipe_.tools.push_back(tool);
    selectedToolId_ = tool.id;
    recomputeAll();
    refreshAll();
    {
        const QSignalBlocker blocker(addCircleButton_);
        addCircleButton_->setChecked(false);
    }
    imageView_->setCreateMode(false);
}

void MainWindow::deleteCurrentCaliper() {
    const int index = currentToolIndex();
    if (index < 0) {
        return;
    }
    const std::string deletedToolId = recipe_.tools[static_cast<size_t>(index)].id;
    recipe_.tools.erase(recipe_.tools.begin() + index);
    recipe_.measurements.erase(
        std::remove_if(
            recipe_.measurements.begin(),
            recipe_.measurements.end(),
            [&](const measure::EdgePairMeasurement& measurement) {
                return measurement.toolAId == deletedToolId || measurement.toolBId == deletedToolId;
            }),
        recipe_.measurements.end());
    if (pendingEdgeToolId_ == deletedToolId) {
        hasPendingEdge_ = false;
        pendingEdgeToolId_.clear();
    }
    if (!recipe_.tools.empty()) {
        const int nextIndex = std::min(index, static_cast<int>(recipe_.tools.size()) - 1);
        selectedToolId_ = recipe_.tools[static_cast<size_t>(nextIndex)].id;
    } else {
        selectedToolId_.clear();
    }
    recomputeAll();
    refreshAll();
}

void MainWindow::selectTool(const QString& id) {
    selectedToolId_ = toStdString(id);
    refreshAll();
}

void MainWindow::updateToolGeometry(const QString& id, QPointF p1, QPointF p2) {
    const int index = findToolIndex(toStdString(id));
    if (index < 0) {
        return;
    }
    auto& tool = recipe_.tools[static_cast<size_t>(index)];
    const cv::Point2d locatorOffset = currentLocatorOffset();
    tool.p1 = {p1.x() - locatorOffset.x, p1.y() - locatorOffset.y};
    tool.p2 = {p2.x() - locatorOffset.x, p2.y() - locatorOffset.y};
    recomputeAll();
    refreshAll();
}

void MainWindow::updateCircleGeometry(const QString& id, QPointF center, double innerRadius, double outerRadius) {
    const int index = findToolIndex(toStdString(id));
    if (index < 0) {
        return;
    }
    auto& tool = recipe_.tools[static_cast<size_t>(index)];
    const cv::Point2d locatorOffset = currentLocatorOffset();
    tool.center = {center.x() - locatorOffset.x, center.y() - locatorOffset.y};
    tool.innerRadius = innerRadius;
    tool.outerRadius = outerRadius;
    recomputeAll();
    refreshAll();
}

void MainWindow::updateTemplateGeometry(const QString& id, QRectF templateRoi, QRectF searchRoi) {
    const int index = findToolIndex(toStdString(id));
    const cv::Mat& source = currentMeasurementImage();
    if (index < 0 || source.empty()) {
        return;
    }
    auto& tool = recipe_.tools[static_cast<size_t>(index)];
    tool.templateRoi = {templateRoi.x(), templateRoi.y(), templateRoi.width(), templateRoi.height()};
    tool.searchRoi = {searchRoi.x(), searchRoi.y(), searchRoi.width(), searchRoi.height()};

    const cv::Rect imageRect(0, 0, source.cols, source.rows);
    cv::Rect roi(
        static_cast<int>(std::floor(templateRoi.x())),
        static_cast<int>(std::floor(templateRoi.y())),
        static_cast<int>(std::ceil(templateRoi.width())),
        static_cast<int>(std::ceil(templateRoi.height())));
    roi &= imageRect;
    if (roi.width > 0 && roi.height > 0) {
        std::vector<uchar> png;
        cv::imencode(".png", source(roi), png);
        const QByteArray bytes(reinterpret_cast<const char*>(png.data()), static_cast<int>(png.size()));
        tool.templateImageBase64Png = bytes.toBase64().toStdString();
    }
    recomputeAll();
    refreshAll();
}

void MainWindow::handleEdgeClicked(QString toolId, int edgeIndex, double edgePosition) {
    (void)edgeIndex;
    const std::string clickedToolId = toStdString(toolId);
    selectedToolId_ = clickedToolId;

    if (!hasPendingEdge_) {
        hasPendingEdge_ = true;
        pendingEdgeToolId_ = clickedToolId;
        pendingEdgePosition_ = edgePosition;
        refreshAll();
        setStatus(QStringLiteral("已选择边 A，请点击任意线卡尺上的边 B"));
        return;
    }

    if (pendingEdgeToolId_ == clickedToolId && std::abs(edgePosition - pendingEdgePosition_) < 1e-6) {
        setStatus(QStringLiteral("请选择另一条边作为边 B"));
        return;
    }

    measure::EdgePairMeasurement measurement;
    measurement.id = toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    measurement.name = toStdString(makeDefaultMeasurementName());
    measurement.toolAId = pendingEdgeToolId_;
    measurement.toolBId = clickedToolId;
    measurement.edgeAPosition = pendingEdgePosition_;
    measurement.edgeBPosition = edgePosition;
    recipe_.measurements.push_back(measurement);

    hasPendingEdge_ = false;
    pendingEdgeToolId_.clear();
    pendingEdgePosition_ = 0.0;
    refreshAll();
    setStatus(QStringLiteral("已生成边距测量项：") + toQString(measurement.name));
}

void MainWindow::applyParametersToCurrentTool() {
    if (updatingUi_) {
        return;
    }
    auto* tool = currentTool();
    if (!tool) {
        return;
    }

    tool->name = toStdString(nameEdit_->text());
    tool->width = widthSpin_->value();
    tool->profileSmoothSigma = profileSigmaSpin_->value();
    tool->derivativeSigma = derivativeSigmaSpin_->value();
    tool->positiveThreshold = positiveThresholdSpin_->value();
    tool->negativeThreshold = negativeThresholdSpin_->value();
    tool->center = {centerXSpin_->value(), centerYSpin_->value()};
    tool->innerRadius = innerRadiusSpin_->value();
    tool->outerRadius = outerRadiusSpin_->value();
    tool->sampleCount = sampleCountSpin_->value();
    tool->circleFitMethod = circleFitCombo_->currentIndex() == 1
        ? measure::CircleFitMethod::Ransac
        : measure::CircleFitMethod::LeastSquares;
    tool->ransacThreshold = ransacThresholdSpin_->value();
    tool->ransacIterations = ransacIterationsSpin_->value();
    tool->templateScoreThreshold = templateThresholdSpin_->value();
    switch (polarityCombo_->currentIndex()) {
    case 1:
        tool->polarity = measure::EdgePolarity::DarkToBright;
        break;
    case 2:
        tool->polarity = measure::EdgePolarity::BrightToDark;
        break;
    default:
        tool->polarity = measure::EdgePolarity::Any;
        break;
    }
    switch (edgePickCombo_->currentIndex()) {
    case 1:
        tool->edgePickMode = measure::EdgePickMode::First;
        break;
    case 2:
        tool->edgePickMode = measure::EdgePickMode::Last;
        break;
    default:
        tool->edgePickMode = measure::EdgePickMode::Strongest;
        break;
    }

    recomputeAll();
    refreshAll();
}

void MainWindow::applyThresholdsFromCurve(double positiveThreshold, double negativeThreshold) {
    auto* tool = currentTool();
    if (!tool || tool->type != measure::ToolType::LineCaliper) {
        return;
    }
    tool->positiveThreshold = positiveThreshold;
    tool->negativeThreshold = negativeThreshold;
    recomputeAll();
    refreshAll();
}

void MainWindow::recomputeAll() {
    runResult_ = runner_.run(recipe_, colorBgr_);
    results_ = runResult_.toolResults;
}

void MainWindow::refreshAll(bool resetImageView) {
    image_ = currentPreviewImage();
    refreshToolList();
    refreshParameterPanel();
    refreshResultTable();
    refreshCurve();
    const auto& displayTools = runResult_.runtimeTools.size() == recipe_.tools.size()
        ? runResult_.runtimeTools
        : recipe_.tools;
    imageView_->setScene(
        image_,
        displayTools,
        results_,
        recipe_.measurements,
        selectedToolId_,
        hasPendingEdge_,
        pendingEdgeToolId_,
        pendingEdgePosition_,
        recipe_.calibration.enabled,
        recipe_.calibration.mmPerPixel,
        resetImageView);
}

void MainWindow::refreshToolList() {
    updatingUi_ = true;
    toolList_->clear();
    int selectedRow = -1;
    for (int i = 0; i < static_cast<int>(recipe_.tools.size()); ++i) {
        const auto& tool = recipe_.tools[static_cast<size_t>(i)];
        const QString typeLabel = tool.type == measure::ToolType::TemplateLocator ? QStringLiteral("[模板] ") :
                                  tool.type == measure::ToolType::CircleCaliper ? QStringLiteral("[圆] ") :
                                                                                  QStringLiteral("[线] ");
        auto* item = new QListWidgetItem(typeLabel + toQString(tool.name), toolList_);
        item->setData(Qt::UserRole, toQString(tool.id));
        if (tool.id == selectedToolId_) {
            selectedRow = i;
        }
    }
    if (selectedRow >= 0) {
        toolList_->setCurrentRow(selectedRow);
    }
    updatingUi_ = false;
}

void MainWindow::refreshParameterPanel() {
    updatingUi_ = true;
    auto* tool = currentTool();
    const bool enabled = tool != nullptr;
    nameEdit_->setEnabled(enabled);
    deleteCaliperButton_->setEnabled(enabled);
    widthSpin_->setEnabled(enabled);
    profileSigmaSpin_->setEnabled(enabled);
    derivativeSigmaSpin_->setEnabled(enabled);
    positiveThresholdSpin_->setEnabled(enabled);
    negativeThresholdSpin_->setEnabled(enabled);
    polarityCombo_->setEnabled(enabled);
    edgePickCombo_->setEnabled(enabled);
    centerXSpin_->setEnabled(enabled);
    centerYSpin_->setEnabled(enabled);
    innerRadiusSpin_->setEnabled(enabled);
    outerRadiusSpin_->setEnabled(enabled);
    sampleCountSpin_->setEnabled(enabled);
    circleFitCombo_->setEnabled(enabled);
    ransacThresholdSpin_->setEnabled(enabled);
    ransacIterationsSpin_->setEnabled(enabled);
    templateThresholdSpin_->setEnabled(enabled);

    calibrationEnabled_->setChecked(recipe_.calibration.enabled);
    mmPerPixelSpin_->setValue(recipe_.calibration.mmPerPixel);
    {
        const QSignalBlocker blocker(channelCombo_);
        channelCombo_->setCurrentIndex(recipe_.defaultChannel == measure::ImageChannel::Red ? 1 :
                                       recipe_.defaultChannel == measure::ImageChannel::Green ? 2 :
                                       recipe_.defaultChannel == measure::ImageChannel::Blue ? 3 : 0);
    }

    if (tool) {
        const bool isLine = tool->type == measure::ToolType::LineCaliper;
        const bool isCircle = tool->type == measure::ToolType::CircleCaliper;
        const bool isTemplate = tool->type == measure::ToolType::TemplateLocator;
        toolParamStack_->setCurrentWidget(isCircle ? circleParamPage_ : isTemplate ? templateParamPage_ : lineParamPage_);
        edgeParamGroup_->setVisible(isLine || isCircle);
        advancedParamGroup_->setVisible(isLine || isCircle);
        widthSpin_->setEnabled(isLine || isCircle);
        profileSigmaSpin_->setEnabled(isLine || isCircle);
        derivativeSigmaSpin_->setEnabled(isLine || isCircle);
        positiveThresholdSpin_->setEnabled(isLine || isCircle);
        negativeThresholdSpin_->setEnabled(isLine || isCircle);
        polarityCombo_->setEnabled(isLine || isCircle);
        edgePickCombo_->setEnabled(isLine || isCircle);
        centerXSpin_->setEnabled(isCircle);
        centerYSpin_->setEnabled(isCircle);
        innerRadiusSpin_->setEnabled(isCircle);
        outerRadiusSpin_->setEnabled(isCircle);
        sampleCountSpin_->setEnabled(isCircle);
        circleFitCombo_->setEnabled(isCircle);
        ransacThresholdSpin_->setEnabled(isCircle);
        ransacIterationsSpin_->setEnabled(isCircle);
        templateThresholdSpin_->setEnabled(isTemplate);
        nameEdit_->setText(toQString(tool->name));
        widthSpin_->setValue(tool->width);
        profileSigmaSpin_->setValue(tool->profileSmoothSigma);
        derivativeSigmaSpin_->setValue(tool->derivativeSigma);
        positiveThresholdSpin_->setValue(tool->positiveThreshold);
        negativeThresholdSpin_->setValue(tool->negativeThreshold);
        polarityCombo_->setCurrentIndex(tool->polarity == measure::EdgePolarity::DarkToBright ? 1 :
                                       tool->polarity == measure::EdgePolarity::BrightToDark ? 2 : 0);
        edgePickCombo_->setCurrentIndex(tool->edgePickMode == measure::EdgePickMode::First ? 1 :
                                       tool->edgePickMode == measure::EdgePickMode::Last ? 2 : 0);
        centerXSpin_->setValue(tool->center.x);
        centerYSpin_->setValue(tool->center.y);
        innerRadiusSpin_->setValue(tool->innerRadius);
        outerRadiusSpin_->setValue(tool->outerRadius);
        sampleCountSpin_->setValue(tool->sampleCount);
        circleFitCombo_->setCurrentIndex(tool->circleFitMethod == measure::CircleFitMethod::Ransac ? 1 : 0);
        ransacThresholdSpin_->setValue(tool->ransacThreshold);
        ransacIterationsSpin_->setValue(tool->ransacIterations);
        templateThresholdSpin_->setValue(tool->templateScoreThreshold);
    } else {
        nameEdit_->clear();
        edgeParamGroup_->setVisible(false);
        advancedParamGroup_->setVisible(false);
    }
    updatingUi_ = false;
}

void MainWindow::refreshResultTable() {
    const int toolRows = static_cast<int>(recipe_.tools.size());
    const int measurementRows = static_cast<int>(recipe_.measurements.size());
    resultTable_->setRowCount(toolRows + measurementRows);

    for (int i = 0; i < toolRows; ++i) {
        const auto& tool = recipe_.tools[static_cast<size_t>(i)];
        const auto& result = i < static_cast<int>(results_.size())
            ? results_[static_cast<size_t>(i)]
            : measure::CaliperResult();
        const auto* edge = result.selectedEdge();

        auto setItem = [&](int column, const QString& text) {
            resultTable_->setItem(i, column, new QTableWidgetItem(text));
        };

        setItem(0, toQString(tool.name));
        if (tool.type == measure::ToolType::TemplateLocator) {
            setItem(1, QStringLiteral("模板"));
            setItem(2, result.ok ? QStringLiteral("OK") : QStringLiteral("NG"));
            setItem(3, QStringLiteral("-"));
            setItem(4, QString::number(result.offset.x, 'f', 3));
            setItem(5, QString::number(result.offset.y, 'f', 3));
            setItem(6, QString::number(result.matchScore, 'f', 4));
            setItem(7, QString());
            setItem(8, toQString(result.message));
            continue;
        }

        if (tool.type == measure::ToolType::CircleCaliper) {
            setItem(1, QStringLiteral("圆卡尺"));
            setItem(2, result.ok ? QStringLiteral("OK") : QStringLiteral("NG"));
            setItem(3, QString::number(result.fitPointCount));
            setItem(4, result.ok ? QString::number(result.fittedCenter.x, 'f', 3) : QString());
            setItem(5, result.ok ? QString::number(result.fittedCenter.y, 'f', 3) : QString());
            setItem(6, result.ok ? QString::number(result.fittedDiameter, 'f', 3) : QString());
            setItem(7, result.ok && recipe_.calibration.enabled
                ? QString::number(result.fittedDiameter * recipe_.calibration.mmPerPixel, 'f', 6)
                : QString());
            setItem(8, result.ok
                ? QStringLiteral("RMS:%1 Inliers:%2")
                    .arg(QString::number(result.fitRmsError, 'f', 3))
                    .arg(result.ransacInlierCount)
                : toQString(result.message));
            continue;
        }

        setItem(1, QStringLiteral("线卡尺"));
        setItem(2, result.ok ? QStringLiteral("OK") : QStringLiteral("NG"));
        setItem(3, QString::number(result.edges.size()));
        setItem(4, edge ? QString::number(edge->point.x, 'f', 3) : QString());
        setItem(5, edge ? QString::number(edge->point.y, 'f', 3) : QString());
        setItem(6, edge ? QString::number(edge->position, 'f', 3) : QString());
        setItem(7, edge && recipe_.calibration.enabled
            ? QString::number(edge->position * recipe_.calibration.mmPerPixel, 'f', 6)
            : QString());
        setItem(8, edge ? QString::number(edge->gradient, 'f', 3) : toQString(result.message));
    }

    for (int i = 0; i < measurementRows; ++i) {
        const int row = toolRows + i;
        const auto& measurement = recipe_.measurements[static_cast<size_t>(i)];
        const MeasurementResolve resolved = resolveMeasurement(measurement, recipe_.tools, results_);

        auto setItem = [&](int column, const QString& text) {
            resultTable_->setItem(row, column, new QTableWidgetItem(text));
        };

        setItem(0, toQString(measurement.name));
        setItem(1, QStringLiteral("边距"));
        setItem(2, resolved.ok ? QStringLiteral("OK") : QStringLiteral("NG"));
        setItem(3, resolved.ok ? QStringLiteral("2") : QStringLiteral("0"));

        if (resolved.ok) {
            const auto& resultA = results_[static_cast<size_t>(resolved.toolAIndex)];
            const auto& resultB = results_[static_cast<size_t>(resolved.toolBIndex)];
            const auto& edgeA = resultA.edges[static_cast<size_t>(resolved.edgeAIndex)];
            const auto& edgeB = resultB.edges[static_cast<size_t>(resolved.edgeBIndex)];
            setItem(4, QStringLiteral("A:%1,%2")
                .arg(QString::number(edgeA.point.x, 'f', 2))
                .arg(QString::number(edgeA.point.y, 'f', 2)));
            setItem(5, QStringLiteral("B:%1,%2")
                .arg(QString::number(edgeB.point.x, 'f', 2))
                .arg(QString::number(edgeB.point.y, 'f', 2)));
            setItem(6, QString::number(resolved.distancePx, 'f', 3));
            setItem(7, recipe_.calibration.enabled
                ? QString::number(resolved.distancePx * recipe_.calibration.mmPerPixel, 'f', 6)
                : QString());
            setItem(8, resolved.message);
        } else {
            setItem(4, QString());
            setItem(5, QString());
            setItem(6, QString());
            setItem(7, QString());
            setItem(8, resolved.message);
        }
    }
}

void MainWindow::refreshCurve() {
    const auto* tool = currentTool();
    const auto* result = currentResult();
    const bool showCurve = tool && tool->type == measure::ToolType::LineCaliper;
    curveGroup_->setVisible(showCurve);
    if (showCurve && result) {
        curveWidget_->setData(
            result->profile,
            result->gradient,
            result->profileStartPosition,
            result->profileSampleStep,
            tool->positiveThreshold,
            tool->negativeThreshold);
    } else {
        curveWidget_->setData({}, {}, 0.0, 1.0, 8.0, -8.0);
    }
}

int MainWindow::currentToolIndex() const {
    return findToolIndex(selectedToolId_);
}

int MainWindow::findToolIndex(const std::string& id) const {
    for (int i = 0; i < static_cast<int>(recipe_.tools.size()); ++i) {
        if (recipe_.tools[static_cast<size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

measure::CaliperTool* MainWindow::currentTool() {
    const int index = currentToolIndex();
    if (index < 0) {
        return nullptr;
    }
    return &recipe_.tools[static_cast<size_t>(index)];
}

const measure::CaliperResult* MainWindow::currentResult() const {
    const int index = currentToolIndex();
    if (index < 0 || index >= static_cast<int>(results_.size())) {
        return nullptr;
    }
    return &results_[static_cast<size_t>(index)];
}

cv::Point2d MainWindow::currentLocatorOffset() const {
    if (runResult_.hasLocator && runResult_.locatorOk) {
        return runResult_.locatorOffset;
    }
    return {};
}

QString MainWindow::currentToolId() const {
    return toQString(selectedToolId_);
}

const cv::Mat& MainWindow::currentMeasurementImage() const {
    switch (recipe_.defaultChannel) {
    case measure::ImageChannel::Red:
        return red_.empty() ? gray_ : red_;
    case measure::ImageChannel::Green:
        return green_.empty() ? gray_ : green_;
    case measure::ImageChannel::Blue:
        return blue_.empty() ? gray_ : blue_;
    case measure::ImageChannel::Gray:
    default:
        return gray_;
    }
}

QImage MainWindow::currentPreviewImage() const {
    return grayToQImage(currentMeasurementImage());
}

QImage MainWindow::grayToQImage(const cv::Mat& gray) const {
    if (gray.empty()) {
        return {};
    }
    return QImage(gray.data, gray.cols, gray.rows, static_cast<int>(gray.step), QImage::Format_Grayscale8).copy();
}

QString MainWindow::makeDefaultToolName() const {
    return QStringLiteral("卡尺%1").arg(recipe_.tools.size() + 1);
}

QString MainWindow::makeDefaultMeasurementName() const {
    return QStringLiteral("边距%1").arg(recipe_.measurements.size() + 1);
}

void MainWindow::setStatus(const QString& text) {
    statusBar()->showMessage(text, 5000);
}
