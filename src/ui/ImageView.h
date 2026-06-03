#pragma once

#include "core/CaliperTypes.h"

#include <QImage>
#include <QWidget>

#include <string>
#include <vector>

class ImageView : public QWidget {
    Q_OBJECT

public:
    explicit ImageView(QWidget* parent = nullptr);

    void setScene(
        const QImage& image,
        const std::vector<measure::CaliperTool>& tools,
        const std::vector<measure::CaliperResult>& results,
        const std::vector<measure::EdgePairMeasurement>& measurements,
        const std::string& selectedId,
        bool hasPendingEdge,
        const std::string& pendingEdgeToolId,
        double pendingEdgePosition);
    void setCreateMode(bool enabled);

signals:
    void caliperCreated(QPointF p1, QPointF p2);
    void selectedToolChanged(QString id);
    void toolGeometryChanged(QString id, QPointF p1, QPointF p2);
    void edgeClicked(QString toolId, int edgeIndex, double edgePosition);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class DragMode {
        None,
        Creating,
        MoveP1,
        MoveP2,
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
    bool createMode_ = false;
    double zoom_ = 1.0;
    QPointF pan_;
    DragMode dragMode_ = DragMode::None;
    int dragToolIndex_ = -1;
    QPointF lastImagePoint_;
    QPointF lastWidgetPoint_;
    cv::Point2d dragOriginalP1_;
    cv::Point2d dragOriginalP2_;
    QPointF previewP1_;
    QPointF previewP2_;
};
