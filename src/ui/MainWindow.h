#pragma once

#include "core/CaliperDetector.h"
#include "core/Recipe.h"
#include "core/RecipeRunner.h"
#include "ui/CurveWidget.h"
#include "ui/ImageView.h"

#include <QMainWindow>
#include <QRectF>
#include <QStringList>

#include <opencv2/core.hpp>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QStackedWidget;
class QWidget;
class QProgressBar;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void connectUi();
    bool loadImage(const QString& path, QString* errorMessage);
    void openImage();
    void saveRecipe();
    void loadRecipe();
    void exportCsv();
    void selectBatchFolder();
    void startBatch();
    void toggleBatchPause();
    void cancelBatch();
    void processNextBatchImage();
    void exportBatchCsv();
    void exportBatchCsvAs();
    bool writeBatchCsv(const QString& path, QString* errorMessage) const;
    void addCaliper(QPointF p1, QPointF p2);
    void addTemplateLocator(QRectF roi);
    void addCircleCaliper(QPointF center, double innerRadius, double outerRadius);
    void deleteCurrentCaliper();
    void selectTool(const QString& id);
    void updateToolGeometry(const QString& id, QPointF p1, QPointF p2);
    void updateCircleGeometry(const QString& id, QPointF center, double innerRadius, double outerRadius);
    void updateTemplateGeometry(const QString& id, QRectF templateRoi, QRectF searchRoi);
    void handleEdgeClicked(QString toolId, int edgeIndex, double edgePosition);
    void applyParametersToCurrentTool();
    void applyThresholdsFromCurve(double positiveThreshold, double negativeThreshold);
    void recomputeAll();
    void refreshAll(bool resetImageView = false);
    void refreshToolList();
    void refreshParameterPanel();
    void refreshResultTable();
    void refreshCurve();
    int currentToolIndex() const;
    int findToolIndex(const std::string& id) const;
    measure::CaliperTool* currentTool();
    const measure::CaliperResult* currentResult() const;
    cv::Point2d currentLocatorOffset() const;
    QString currentToolId() const;
    const cv::Mat& currentMeasurementImage() const;
    QImage currentPreviewImage() const;
    QImage grayToQImage(const cv::Mat& gray) const;
    QString makeDefaultToolName() const;
    QString makeDefaultMeasurementName() const;
    void setStatus(const QString& text);

    struct BatchRow {
        QString imagePath;
        QString name;
        QString type;
        QString status;
        QString value;
        QString unit;
        QString message;
    };

    measure::Recipe recipe_;
    measure::CaliperDetector detector_;
    measure::RecipeRunner runner_;
    measure::RecipeRunResult runResult_;
    std::vector<measure::CaliperResult> results_;
    cv::Mat colorBgr_;
    cv::Mat gray_;
    cv::Mat red_;
    cv::Mat green_;
    cv::Mat blue_;
    QImage image_;
    std::string selectedToolId_;
    bool hasPendingEdge_ = false;
    std::string pendingEdgeToolId_;
    double pendingEdgePosition_ = 0.0;
    bool updatingUi_ = false;
    QString batchFolder_;
    QStringList batchFiles_;
    int batchIndex_ = 0;
    bool batchRunning_ = false;
    bool batchPaused_ = false;
    bool batchCancelRequested_ = false;
    std::vector<BatchRow> batchRows_;

    ImageView* imageView_ = nullptr;
    QListWidget* toolList_ = nullptr;
    QPushButton* addCaliperButton_ = nullptr;
    QPushButton* addTemplateButton_ = nullptr;
    QPushButton* addCircleButton_ = nullptr;
    QPushButton* deleteCaliperButton_ = nullptr;
    QPushButton* openImageButton_ = nullptr;
    QPushButton* saveRecipeButton_ = nullptr;
    QPushButton* loadRecipeButton_ = nullptr;
    QPushButton* exportCsvButton_ = nullptr;
    QPushButton* selectBatchFolderButton_ = nullptr;
    QPushButton* startBatchButton_ = nullptr;
    QPushButton* pauseBatchButton_ = nullptr;
    QPushButton* cancelBatchButton_ = nullptr;
    QPushButton* exportBatchCsvButton_ = nullptr;
    QPushButton* fitImageButton_ = nullptr;
    QComboBox* batchNgModeCombo_ = nullptr;
    QProgressBar* batchProgress_ = nullptr;
    QTableWidget* batchTable_ = nullptr;
    QTimer* batchTimer_ = nullptr;
    QCheckBox* calibrationEnabled_ = nullptr;
    QDoubleSpinBox* mmPerPixelSpin_ = nullptr;
    QComboBox* channelCombo_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QStackedWidget* toolParamStack_ = nullptr;
    QWidget* lineParamPage_ = nullptr;
    QWidget* circleParamPage_ = nullptr;
    QWidget* templateParamPage_ = nullptr;
    QGroupBox* edgeParamGroup_ = nullptr;
    QGroupBox* advancedParamGroup_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* profileSigmaSpin_ = nullptr;
    QDoubleSpinBox* derivativeSigmaSpin_ = nullptr;
    QDoubleSpinBox* positiveThresholdSpin_ = nullptr;
    QDoubleSpinBox* negativeThresholdSpin_ = nullptr;
    QComboBox* polarityCombo_ = nullptr;
    QComboBox* edgePickCombo_ = nullptr;
    QDoubleSpinBox* centerXSpin_ = nullptr;
    QDoubleSpinBox* centerYSpin_ = nullptr;
    QDoubleSpinBox* innerRadiusSpin_ = nullptr;
    QDoubleSpinBox* outerRadiusSpin_ = nullptr;
    QSpinBox* sampleCountSpin_ = nullptr;
    QComboBox* circleFitCombo_ = nullptr;
    QDoubleSpinBox* ransacThresholdSpin_ = nullptr;
    QSpinBox* ransacIterationsSpin_ = nullptr;
    QDoubleSpinBox* templateThresholdSpin_ = nullptr;
    QComboBox* curveModeCombo_ = nullptr;
    CurveWidget* curveWidget_ = nullptr;
    QGroupBox* curveGroup_ = nullptr;
    QTableWidget* resultTable_ = nullptr;
};
