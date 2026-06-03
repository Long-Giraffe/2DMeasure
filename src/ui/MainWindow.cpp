#include "ui/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextStream>
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
    int toolIndex = -1;
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
        if (tools[static_cast<size_t>(i)].id == measurement.caliperToolId) {
            resolved.toolIndex = i;
            break;
        }
    }
    if (resolved.toolIndex < 0 || resolved.toolIndex >= static_cast<int>(results.size())) {
        resolved.message = QStringLiteral("Caliper missing");
        return resolved;
    }

    const auto& result = results[static_cast<size_t>(resolved.toolIndex)];
    resolved.edgeAIndex = nearestEdgeIndex(result, measurement.edgeAPosition);
    resolved.edgeBIndex = nearestEdgeIndex(result, measurement.edgeBPosition);
    if (resolved.edgeAIndex < 0 || resolved.edgeBIndex < 0) {
        resolved.message = QStringLiteral("Edge missing");
        return resolved;
    }
    if (resolved.edgeAIndex == resolved.edgeBIndex) {
        resolved.message = QStringLiteral("Same edge");
        return resolved;
    }

    const auto& edgeA = result.edges[static_cast<size_t>(resolved.edgeAIndex)];
    const auto& edgeB = result.edges[static_cast<size_t>(resolved.edgeBIndex)];
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
    resize(1500, 900);

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(6, 6, 6, 6);
    setCentralWidget(central);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    rootLayout->addWidget(splitter);

    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);

    auto* fileGroup = new QGroupBox(QStringLiteral("图像 / 配方"), leftPanel);
    auto* fileLayout = new QVBoxLayout(fileGroup);
    openImageButton_ = new QPushButton(QStringLiteral("打开图像"), fileGroup);
    loadRecipeButton_ = new QPushButton(QStringLiteral("加载配方"), fileGroup);
    saveRecipeButton_ = new QPushButton(QStringLiteral("保存配方"), fileGroup);
    exportCsvButton_ = new QPushButton(QStringLiteral("导出 CSV"), fileGroup);
    fileLayout->addWidget(openImageButton_);
    fileLayout->addWidget(loadRecipeButton_);
    fileLayout->addWidget(saveRecipeButton_);
    fileLayout->addWidget(exportCsvButton_);
    leftLayout->addWidget(fileGroup);

    auto* calibrationGroup = new QGroupBox(QStringLiteral("比例尺"), leftPanel);
    auto* calibrationLayout = new QFormLayout(calibrationGroup);
    calibrationEnabled_ = new QCheckBox(QStringLiteral("启用毫米换算"), calibrationGroup);
    mmPerPixelSpin_ = new QDoubleSpinBox(calibrationGroup);
    mmPerPixelSpin_->setRange(0.000001, 1000000.0);
    mmPerPixelSpin_->setDecimals(6);
    mmPerPixelSpin_->setValue(0.01);
    calibrationLayout->addRow(calibrationEnabled_);
    calibrationLayout->addRow(QStringLiteral("mm/px"), mmPerPixelSpin_);
    leftLayout->addWidget(calibrationGroup);

    auto* toolsGroup = new QGroupBox(QStringLiteral("卡尺列表"), leftPanel);
    auto* toolsLayout = new QVBoxLayout(toolsGroup);
    addCaliperButton_ = new QPushButton(QStringLiteral("新增卡尺"), toolsGroup);
    addCaliperButton_->setCheckable(true);
    deleteCaliperButton_ = new QPushButton(QStringLiteral("删除选中卡尺"), toolsGroup);
    toolList_ = new QListWidget(toolsGroup);
    toolsLayout->addWidget(addCaliperButton_);
    toolsLayout->addWidget(deleteCaliperButton_);
    toolsLayout->addWidget(toolList_, 1);
    leftLayout->addWidget(toolsGroup, 1);
    splitter->addWidget(leftPanel);

    auto* centerPanel = new QWidget(splitter);
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    imageView_ = new ImageView(centerPanel);
    centerLayout->addWidget(imageView_, 1);

    auto* centerCurveGroup = new QGroupBox(QStringLiteral("灰度 / 导数曲线"), centerPanel);
    auto* centerCurveLayout = new QVBoxLayout(centerCurveGroup);
    auto* centerCurveModeCombo = new QComboBox(centerCurveGroup);
    centerCurveModeCombo->addItems({QStringLiteral("灰度+导数"), QStringLiteral("灰度"), QStringLiteral("导数")});
    auto* centerCurveWidget = new CurveWidget(centerCurveGroup);
    centerCurveLayout->addWidget(centerCurveModeCombo);
    centerCurveLayout->addWidget(centerCurveWidget, 1);
    centerLayout->addWidget(centerCurveGroup, 0);
    splitter->addWidget(centerPanel);

    auto* rightPanel = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);

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

    paramLayout->addRow(QStringLiteral("名称"), nameEdit_);
    paramLayout->addRow(QStringLiteral("宽度"), widthSpin_);
    paramLayout->addRow(QStringLiteral("灰度平滑 σ"), profileSigmaSpin_);
    paramLayout->addRow(QStringLiteral("导数 σ"), derivativeSigmaSpin_);
    paramLayout->addRow(QStringLiteral("正阈值"), positiveThresholdSpin_);
    paramLayout->addRow(QStringLiteral("负阈值"), negativeThresholdSpin_);
    paramLayout->addRow(QStringLiteral("极性"), polarityCombo_);
    paramLayout->addRow(QStringLiteral("边缘选择"), edgePickCombo_);
    rightLayout->addWidget(paramGroup);

    curveModeCombo_ = centerCurveModeCombo;
    curveWidget_ = centerCurveWidget;

    resultTable_ = new QTableWidget(rightPanel);
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
    resultTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    resultTable_->verticalHeader()->setVisible(false);
    rightLayout->addWidget(resultTable_, 1);
    splitter->addWidget(rightPanel);

    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({280, 820, 400});
}

void MainWindow::connectUi() {
    connect(openImageButton_, &QPushButton::clicked, this, &MainWindow::openImage);
    connect(saveRecipeButton_, &QPushButton::clicked, this, &MainWindow::saveRecipe);
    connect(loadRecipeButton_, &QPushButton::clicked, this, &MainWindow::loadRecipe);
    connect(exportCsvButton_, &QPushButton::clicked, this, &MainWindow::exportCsv);
    connect(deleteCaliperButton_, &QPushButton::clicked, this, &MainWindow::deleteCurrentCaliper);
    connect(addCaliperButton_, &QPushButton::toggled, imageView_, &ImageView::setCreateMode);
    connect(imageView_, &ImageView::caliperCreated, this, &MainWindow::addCaliper);
    connect(imageView_, &ImageView::selectedToolChanged, this, &MainWindow::selectTool);
    connect(imageView_, &ImageView::toolGeometryChanged, this, &MainWindow::updateToolGeometry);
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
        refreshResultTable();
    });
    connect(mmPerPixelSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        recipe_.calibration.mmPerPixel = value;
        refreshResultTable();
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
    cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_GRAYSCALE);
    if (decoded.empty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("OpenCV 无法解码该图像");
        }
        return false;
    }

    gray_ = decoded;
    image_ = grayToQImage(gray_);
    recipe_.imagePath = toStdString(path);
    recomputeAll();
    refreshAll();
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
            const auto& result = results_[static_cast<size_t>(resolved.toolIndex)];
            const auto& edgeA = result.edges[static_cast<size_t>(resolved.edgeAIndex)];
            const auto& edgeB = result.edges[static_cast<size_t>(resolved.edgeBIndex)];
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

void MainWindow::addCaliper(QPointF p1, QPointF p2) {
    measure::CaliperTool tool;
    tool.id = toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    tool.name = toStdString(makeDefaultToolName());
    tool.p1 = {p1.x(), p1.y()};
    tool.p2 = {p2.x(), p2.y()};
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
                return measurement.caliperToolId == deletedToolId;
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
    tool.p1 = {p1.x(), p1.y()};
    tool.p2 = {p2.x(), p2.y()};
    recomputeAll();
    refreshAll();
}

void MainWindow::handleEdgeClicked(QString toolId, int edgeIndex, double edgePosition) {
    (void)edgeIndex;
    const std::string clickedToolId = toStdString(toolId);
    selectedToolId_ = clickedToolId;

    if (!hasPendingEdge_ || pendingEdgeToolId_ != clickedToolId) {
        hasPendingEdge_ = true;
        pendingEdgeToolId_ = clickedToolId;
        pendingEdgePosition_ = edgePosition;
        refreshAll();
        setStatus(QStringLiteral("已选择边 A，请点击同一卡尺上的边 B"));
        return;
    }

    if (std::abs(edgePosition - pendingEdgePosition_) < 1e-6) {
        setStatus(QStringLiteral("请选择另一条边作为边 B"));
        return;
    }

    measure::EdgePairMeasurement measurement;
    measurement.id = toStdString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    measurement.name = toStdString(makeDefaultMeasurementName());
    measurement.caliperToolId = clickedToolId;
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
    if (!tool) {
        return;
    }
    tool->positiveThreshold = positiveThreshold;
    tool->negativeThreshold = negativeThreshold;
    recomputeAll();
    refreshAll();
}

void MainWindow::recomputeAll() {
    results_.clear();
    results_.reserve(recipe_.tools.size());
    for (const auto& tool : recipe_.tools) {
        results_.push_back(detector_.detect(gray_, tool));
    }
}

void MainWindow::refreshAll() {
    refreshToolList();
    refreshParameterPanel();
    refreshResultTable();
    refreshCurve();
    imageView_->setScene(
        image_,
        recipe_.tools,
        results_,
        recipe_.measurements,
        selectedToolId_,
        hasPendingEdge_,
        pendingEdgeToolId_,
        pendingEdgePosition_);
}

void MainWindow::refreshToolList() {
    updatingUi_ = true;
    toolList_->clear();
    int selectedRow = -1;
    for (int i = 0; i < static_cast<int>(recipe_.tools.size()); ++i) {
        const auto& tool = recipe_.tools[static_cast<size_t>(i)];
        auto* item = new QListWidgetItem(toQString(tool.name), toolList_);
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

    calibrationEnabled_->setChecked(recipe_.calibration.enabled);
    mmPerPixelSpin_->setValue(recipe_.calibration.mmPerPixel);

    if (tool) {
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
    } else {
        nameEdit_->clear();
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
        setItem(1, QStringLiteral("卡尺"));
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
            const auto& result = results_[static_cast<size_t>(resolved.toolIndex)];
            const auto& edgeA = result.edges[static_cast<size_t>(resolved.edgeAIndex)];
            const auto& edgeB = result.edges[static_cast<size_t>(resolved.edgeBIndex)];
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
    if (tool && result) {
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

QString MainWindow::currentToolId() const {
    return toQString(selectedToolId_);
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
