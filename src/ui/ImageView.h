#pragma once

#include "core/CaliperTypes.h"

#include <QImage>
#include <QRectF>
#include <QWidget>

#include <string>
#include <vector>

class ImageView : public QWidget {
    Q_OBJECT

public:
    enum class CreateMode {
        None,
        LineCaliper,
        TemplateLocator,
        CircleCaliper,
    };

    explicit ImageView(QWidget* parent = nullptr);

    void setScene(
        const QImage& image,
        const std::vector<measure::CaliperTool>& tools,
        const std::vector<measure::CaliperResult>& results,
        const std::vector<measure::EdgePairMeasurement>& measurements,
        const std::string& selectedId,
        bool hasPendingEdge,
        const std::string& pendingEdgeToolId,
        double pendingEdgePosition,
        bool calibrationEnabled,
        double mmPerPixel);
    void setCreateMode(bool enabled);
    void setCreateMode(CreateMode mode);

signals:
    void caliperCreated(QPointF p1, QPointF p2);
    void templateCreated(QRectF roi);
    void circleCaliperCreated(QPointF center, double innerRadius, double outerRadius);
    void selectedToolChanged(QString id);
    void toolGeometryChanged(QString id, QPointF p1, QPointF p2);
    void circleGeometryChanged(QString id, QPointF center, double innerRadius, double outerRadius);
    void templateGeometryChanged(QString id, QRectF templateRoi, QRectF searchRoi);
    void edgeClicked(QString toolId, int edgeIndex, double edgePosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class DragMode {
        None,
        Creating,
        MoveP1,
        MoveP2,
        MoveCircleCenter,
        MoveCircleInner,
        MoveCircleOuter,
        MoveTemplateRoi,
        ResizeTemplateRoi,
        MoveSearchRoi,
        ResizeSearchRoi,
        MoveTool,
        Pan,
    };

    struct EdgeHit {
        int toolIndex = -1;
        int edgeIndex = -1;
        double distance = 0.0;
    };

    double imageScale() const;
    QPointF imageOffset() const;
    QPointF imageToWidget(const cv::Point2d& point) const;
    QPointF widgetToImage(const QPointF& point) const;
    bool hasImage() const;
    int findHitTool(const QPointF& widgetPoint, DragMode* mode) const;
    EdgeHit findHitEdge(const QPointF& widgetPoint) const;
    bool isMeasurementEdge(const measure::CaliperTool& tool, const measure::EdgePoint& edge, QString* label) const;

    QImage image_;
    std::vector<measure::CaliperTool> tools_;
    std::vector<measure::CaliperResult> results_;
    std::vector<measure::EdgePairMeasurement> measurements_;
    std::string selectedId_;
    bool hasPendingEdge_ = false;
    std::string pendingEdgeToolId_;
    double pendingEdgePosition_ = 0.0;
    bool calibrationEnabled_ = false;
    double mmPerPixel_ = 0.01;
    CreateMode createMode_ = CreateMode::None;
    double zoom_ = 1.0;
    QPointF pan_;
    DragMode dragMode_ = DragMode::None;
    int dragToolIndex_ = -1;
    QPointF lastImagePoint_;
    QPointF lastWidgetPoint_;
    cv::Point2d dragOriginalP1_;
    cv::Point2d dragOriginalP2_;
    cv::Point2d dragOriginalCenter_;
    double dragOriginalInnerRadius_ = 0.0;
    double dragOriginalOuterRadius_ = 0.0;
    QRectF dragOriginalTemplateRoi_;
    QRectF dragOriginalSearchRoi_;
    QPointF previewP1_;
    QPointF previewP2_;
    QPointF mouseImagePoint_;
    bool hasMouseImagePoint_ = false;
};
