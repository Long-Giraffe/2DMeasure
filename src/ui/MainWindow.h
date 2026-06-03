#pragma once

#include "core/CaliperDetector.h"
#include "core/Recipe.h"
#include "ui/CurveWidget.h"
#include "ui/ImageView.h"

#include <QMainWindow>

#include <opencv2/core.hpp>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

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
    void addCaliper(QPointF p1, QPointF p2);
    void deleteCurrentCaliper();
    void selectTool(const QString& id);
    void updateToolGeometry(const QString& id, QPointF p1, QPointF p2);
    void handleEdgeClicked(QString toolId, int edgeIndex, double edgePosition);
    void applyParametersToCurrentTool();
    void applyThresholdsFromCurve(double positiveThreshold, double negativeThreshold);
    void recomputeAll();
    void refreshAll();
    void refreshToolList();
    void refreshParameterPanel();
    void refreshResultTable();
    void refreshCurve();
    int currentToolIndex() const;
    int findToolIndex(const std::string& id) const;
    measure::CaliperTool* currentTool();
    const measure::CaliperResult* currentResult() const;
    QString currentToolId() const;
    QImage grayToQImage(const cv::Mat& gray) const;
    QString makeDefaultToolName() const;
    QString makeDefaultMeasurementName() const;
    void setStatus(const QString& text);

    measure::Recipe recipe_;
    measure::CaliperDetector detector_;
    std::vector<measure::CaliperResult> results_;
    cv::Mat gray_;
    QImage image_;
    std::string selectedToolId_;
    bool hasPendingEdge_ = false;
    std::string pendingEdgeToolId_;
    double pendingEdgePosition_ = 0.0;
    bool updatingUi_ = false;

    ImageView* imageView_ = nullptr;
    QListWidget* toolList_ = nullptr;
    QPushButton* addCaliperButton_ = nullptr;
    QPushButton* deleteCaliperButton_ = nullptr;
    QPushButton* openImageButton_ = nullptr;
    QPushButton* saveRecipeButton_ = nullptr;
    QPushButton* loadRecipeButton_ = nullptr;
    QPushButton* exportCsvButton_ = nullptr;
    QCheckBox* calibrationEnabled_ = nullptr;
    QDoubleSpinBox* mmPerPixelSpin_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* profileSigmaSpin_ = nullptr;
    QDoubleSpinBox* derivativeSigmaSpin_ = nullptr;
    QDoubleSpinBox* positiveThresholdSpin_ = nullptr;
    QDoubleSpinBox* negativeThresholdSpin_ = nullptr;
    QComboBox* polarityCombo_ = nullptr;
    QComboBox* edgePickCombo_ = nullptr;
    QComboBox* curveModeCombo_ = nullptr;
    CurveWidget* curveWidget_ = nullptr;
    QTableWidget* resultTable_ = nullptr;
};
