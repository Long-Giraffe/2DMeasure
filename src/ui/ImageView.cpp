#include "ui/ImageView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>

namespace {

double distancePointToSegment(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const QPointF ap = p - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    if (len2 <= 1e-9) {
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    }
    const double t = std::max(0.0, std::min(1.0, QPointF::dotProduct(ap, ab) / len2));
    const QPointF projection = a + ab * t;
    return std::hypot(p.x() - projection.x(), p.y() - projection.y());
}

} // namespace

ImageView::ImageView(QWidget* parent)
    : QWidget(parent) {
    setMinimumSize(640, 480);
    setMouseTracking(true);
}

void ImageView::setScene(
    const QImage& image,
    const std::vector<measure::CaliperTool>& tools,
    const std::vector<measure::CaliperResult>& results,
    const std::vector<measure::EdgePairMeasurement>& measurements,
    const std::string& selectedId,
    bool hasPendingEdge,
    const std::string& pendingEdgeToolId,
    double pendingEdgePosition) {
    const bool imageChanged = image_.cacheKey() != image.cacheKey();
    image_ = image;
    tools_ = tools;
    results_ = results;
    measurements_ = measurements;
    selectedId_ = selectedId;
    hasPendingEdge_ = hasPendingEdge;
    pendingEdgeToolId_ = pendingEdgeToolId;
    pendingEdgePosition_ = pendingEdgePosition;
    if (imageChanged) {
        zoom_ = 1.0;
        pan_ = QPointF(0.0, 0.0);
    }
    update();
}

void ImageView::setCreateMode(bool enabled) {
    createMode_ = enabled;
    dragMode_ = DragMode::None;
    update();
}

void ImageView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(34, 38, 42));

    if (!hasImage()) {
        painter.setPen(QColor(180, 180, 180));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("打开图像后开始绘制卡尺"));
        return;
    }

    const double scale = imageScale();
    const QPointF offset = imageOffset();
    const QRectF target(offset, QSizeF(image_.width() * scale, image_.height() * scale));
    painter.drawImage(target, image_);

    for (size_t i = 0; i < tools_.size(); ++i) {
        const auto& tool = tools_[i];
        const bool selected = tool.id == selectedId_;
        const QPointF p1 = imageToWidget(tool.p1);
        const QPointF p2 = imageToWidget(tool.p2);
        const QPointF axis = p2 - p1;
        const double len = std::hypot(axis.x(), axis.y());
        QPointF perp(0.0, 0.0);
        if (len > 1e-6) {
            perp = QPointF(-axis.y() / len, axis.x() / len) * (0.5 * tool.width * scale);
        }

        painter.setPen(QPen(selected ? QColor(255, 215, 80) : QColor(80, 210, 255), selected ? 2.5 : 1.5));
        painter.drawLine(p1, p2);
        painter.drawLine(p1 + perp, p1 - perp);
        painter.drawLine(p2 + perp, p2 - perp);
        painter.drawLine(p1 + perp, p2 + perp);
        painter.drawLine(p1 - perp, p2 - perp);
        painter.setBrush(selected ? QColor(255, 215, 80) : QColor(80, 210, 255));
        painter.drawEllipse(p1, selected ? 7.0 : 5.5, selected ? 7.0 : 5.5);
        painter.drawEllipse(p2, selected ? 7.0 : 5.5, selected ? 7.0 : 5.5);

        if (i < results_.size()) {
            const auto& result = results_[i];
            QPointF edgePerp = perp;
            const double edgePerpLen = std::hypot(edgePerp.x(), edgePerp.y());
            if (edgePerpLen > 1e-6 && edgePerpLen < 8.0) {
                edgePerp *= 8.0 / edgePerpLen;
            }

            for (int edgeIndex = 0; edgeIndex < static_cast<int>(result.edges.size()); ++edgeIndex) {
                const auto& edge = result.edges[static_cast<size_t>(edgeIndex)];
                const QPointF ep = imageToWidget(edge.point);
                QColor color = edge.polarity >= 0 ? QColor(255, 70, 70) : QColor(0, 190, 255);
                if (!selected) {
                    color.setAlpha(150);
                }

                QString label;
                const bool marked = isMeasurementEdge(tool, edge, &label);
                const bool selectedByPick = edgeIndex == result.selectedIndex;
                const double penWidth = marked ? 3.4 : (selected ? 2.2 : 1.2);
                painter.setPen(QPen(color, penWidth));
                painter.drawLine(ep + edgePerp, ep - edgePerp);

                if (selectedByPick) {
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(QColor(255, 255, 255), 1.4));
                    painter.drawEllipse(ep, 4.0, 4.0);
                }

                if (marked) {
                    painter.setBrush(QColor(255, 255, 255));
                    painter.setPen(QPen(color, 2.0));
                    const QRectF badge(ep + QPointF(5.0, -20.0), QSizeF(18.0, 16.0));
                    painter.drawRoundedRect(badge, 3.0, 3.0);
                    painter.drawText(badge, Qt::AlignCenter, label);
                }
            }
        }
    }

    if (dragMode_ == DragMode::Creating) {
        painter.setPen(QPen(QColor(255, 215, 80), 2.0, Qt::DashLine));
        painter.drawLine(previewP1_, previewP2_);
    }
}

void ImageView::mousePressEvent(QMouseEvent* event) {
    if (!hasImage()) {
        return;
    }

    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        dragMode_ = DragMode::Pan;
        lastWidgetPoint_ = event->pos();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        return;
    }

    if (createMode_) {
        dragMode_ = DragMode::Creating;
        previewP1_ = event->pos();
        previewP2_ = event->pos();
        return;
    }

    DragMode hitMode = DragMode::None;
    const int hit = findHitTool(event->pos(), &hitMode);
    if (hit >= 0 && (hitMode == DragMode::MoveP1 || hitMode == DragMode::MoveP2)) {
        dragToolIndex_ = hit;
        dragMode_ = hitMode;
        dragOriginalP1_ = tools_[static_cast<size_t>(hit)].p1;
        dragOriginalP2_ = tools_[static_cast<size_t>(hit)].p2;
        lastImagePoint_ = widgetToImage(event->pos());
        emit selectedToolChanged(QString::fromStdString(tools_[static_cast<size_t>(hit)].id));
        return;
    }

    const EdgeHit edgeHit = findHitEdge(event->pos());
    if (edgeHit.toolIndex >= 0 && edgeHit.edgeIndex >= 0) {
        const auto& tool = tools_[static_cast<size_t>(edgeHit.toolIndex)];
        const auto& edge = results_[static_cast<size_t>(edgeHit.toolIndex)].edges[static_cast<size_t>(edgeHit.edgeIndex)];
        const QString clickedToolId = QString::fromStdString(tool.id);
        const double clickedPosition = edge.position;
        emit selectedToolChanged(clickedToolId);
        emit edgeClicked(clickedToolId, edgeHit.edgeIndex, clickedPosition);
        return;
    }

    if (hit >= 0) {
        dragToolIndex_ = hit;
        dragMode_ = hitMode;
        dragOriginalP1_ = tools_[static_cast<size_t>(hit)].p1;
        dragOriginalP2_ = tools_[static_cast<size_t>(hit)].p2;
        lastImagePoint_ = widgetToImage(event->pos());
        emit selectedToolChanged(QString::fromStdString(tools_[static_cast<size_t>(hit)].id));
    }
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    if (dragMode_ == DragMode::Pan) {
        pan_ += event->pos() - lastWidgetPoint_;
        lastWidgetPoint_ = event->pos();
        update();
        return;
    }

    if (dragMode_ == DragMode::Creating) {
        previewP2_ = event->pos();
        update();
        return;
    }

    if (dragMode_ == DragMode::None) {
        DragMode hitMode = DragMode::None;
        const int hit = findHitTool(event->pos(), &hitMode);
        const EdgeHit edgeHit = findHitEdge(event->pos());
        if (hit >= 0 && (hitMode == DragMode::MoveP1 || hitMode == DragMode::MoveP2)) {
            setCursor(Qt::SizeAllCursor);
        } else if (edgeHit.toolIndex >= 0) {
            setCursor(Qt::PointingHandCursor);
        } else if (hit >= 0) {
            setCursor(Qt::SizeAllCursor);
        } else {
            unsetCursor();
        }
        return;
    }

    if (dragToolIndex_ < 0 || dragToolIndex_ >= static_cast<int>(tools_.size())) {
        return;
    }

    const QPointF imagePoint = widgetToImage(event->pos());
    auto tool = tools_[static_cast<size_t>(dragToolIndex_)];
    if (dragMode_ == DragMode::MoveP1) {
        tool.p1 = {imagePoint.x(), imagePoint.y()};
    } else if (dragMode_ == DragMode::MoveP2) {
        tool.p2 = {imagePoint.x(), imagePoint.y()};
    } else if (dragMode_ == DragMode::MoveTool) {
        const QPointF delta = imagePoint - lastImagePoint_;
        tool.p1 = {dragOriginalP1_.x + delta.x(), dragOriginalP1_.y + delta.y()};
        tool.p2 = {dragOriginalP2_.x + delta.x(), dragOriginalP2_.y + delta.y()};
    } else {
        return;
    }

    tools_[static_cast<size_t>(dragToolIndex_)] = tool;
    selectedId_ = tool.id;
    update();

    emit toolGeometryChanged(
        QString::fromStdString(tool.id),
        QPointF(tool.p1.x, tool.p1.y),
        QPointF(tool.p2.x, tool.p2.y));
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (dragMode_ == DragMode::Creating && event->button() == Qt::LeftButton) {
        const QPointF p1 = widgetToImage(previewP1_);
        const QPointF p2 = widgetToImage(previewP2_);
        if (std::hypot(p2.x() - p1.x(), p2.y() - p1.y()) >= 5.0) {
            emit caliperCreated(p1, p2);
        }
    }
    dragMode_ = DragMode::None;
    dragToolIndex_ = -1;
    update();
}

void ImageView::wheelEvent(QWheelEvent* event) {
    if (!hasImage()) {
        return;
    }
    const QPointF before = widgetToImage(event->pos());
    zoom_ *= event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    zoom_ = std::max(0.1, std::min(20.0, zoom_));
    const QPointF afterWidget = imageToWidget({before.x(), before.y()});
    pan_ += event->pos() - afterWidget;
    update();
}

double ImageView::imageScale() const {
    if (!hasImage()) {
        return 1.0;
    }
    const double sx = static_cast<double>(width()) / image_.width();
    const double sy = static_cast<double>(height()) / image_.height();
    return std::min(sx, sy) * zoom_;
}

QPointF ImageView::imageOffset() const {
    if (!hasImage()) {
        return QPointF();
    }
    const double scale = imageScale();
    return QPointF(
        0.5 * (width() - image_.width() * scale),
        0.5 * (height() - image_.height() * scale)) + pan_;
}

QPointF ImageView::imageToWidget(const cv::Point2d& point) const {
    const double scale = imageScale();
    // Internal image coordinates follow OpenCV's pixel-center convention.
    // QPainter draws QImage in pixel-cell coordinates, so add/subtract 0.5
    // when crossing the UI boundary to keep overlays on the visible edge.
    return imageOffset() + QPointF((point.x + 0.5) * scale, (point.y + 0.5) * scale);
}

QPointF ImageView::widgetToImage(const QPointF& point) const {
    const double scale = imageScale();
    const QPointF imagePoint = (point - imageOffset()) / scale - QPointF(0.5, 0.5);
    return QPointF(
        std::max(0.0, std::min(static_cast<double>(image_.width() - 1), imagePoint.x())),
        std::max(0.0, std::min(static_cast<double>(image_.height() - 1), imagePoint.y())));
}

bool ImageView::hasImage() const {
    return !image_.isNull();
}

int ImageView::findHitTool(const QPointF& widgetPoint, DragMode* mode) const {
    int best = -1;
    double bestDistance = 1e9;
    DragMode bestMode = DragMode::None;
    constexpr double endpointRadius = 18.0;
    constexpr double lineRadius = 9.0;

    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        const QPointF p1 = imageToWidget(tools_[static_cast<size_t>(i)].p1);
        const QPointF p2 = imageToWidget(tools_[static_cast<size_t>(i)].p2);
        const double d1 = std::hypot(widgetPoint.x() - p1.x(), widgetPoint.y() - p1.y());
        const double d2 = std::hypot(widgetPoint.x() - p2.x(), widgetPoint.y() - p2.y());

        if (d1 < bestDistance && d1 <= endpointRadius) {
            best = i;
            bestDistance = d1;
            bestMode = DragMode::MoveP1;
        }
        if (d2 < bestDistance && d2 <= endpointRadius) {
            best = i;
            bestDistance = d2;
            bestMode = DragMode::MoveP2;
        }
    }

    if (best >= 0) {
        if (mode) {
            *mode = bestMode;
        }
        return best;
    }

    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        const QPointF p1 = imageToWidget(tools_[static_cast<size_t>(i)].p1);
        const QPointF p2 = imageToWidget(tools_[static_cast<size_t>(i)].p2);
        const double dl = distancePointToSegment(widgetPoint, p1, p2);
        if (dl < bestDistance && dl <= lineRadius) {
            best = i;
            bestDistance = dl;
            bestMode = DragMode::MoveTool;
        }
    }

    if (mode) {
        *mode = bestMode;
    }
    return best;
}

ImageView::EdgeHit ImageView::findHitEdge(const QPointF& widgetPoint) const {
    EdgeHit best;
    best.distance = 1e9;
    constexpr double edgeRadius = 10.0;

    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        if (i >= static_cast<int>(results_.size())) {
            continue;
        }

        const auto& tool = tools_[static_cast<size_t>(i)];
        const QPointF p1 = imageToWidget(tool.p1);
        const QPointF p2 = imageToWidget(tool.p2);
        const QPointF axis = p2 - p1;
        const double len = std::hypot(axis.x(), axis.y());
        if (len <= 1e-6) {
            continue;
        }

        QPointF perp(-axis.y() / len, axis.x() / len);
        perp *= 0.5 * tool.width * imageScale();
        const double perpLen = std::hypot(perp.x(), perp.y());
        if (perpLen > 1e-6 && perpLen < 8.0) {
            perp *= 8.0 / perpLen;
        }

        const auto& result = results_[static_cast<size_t>(i)];
        for (int edgeIndex = 0; edgeIndex < static_cast<int>(result.edges.size()); ++edgeIndex) {
            const auto& edge = result.edges[static_cast<size_t>(edgeIndex)];
            const QPointF ep = imageToWidget(edge.point);
            const double d = distancePointToSegment(widgetPoint, ep + perp, ep - perp);
            if (d < best.distance && d <= edgeRadius) {
                best.toolIndex = i;
                best.edgeIndex = edgeIndex;
                best.distance = d;
            }
        }
    }

    return best;
}

bool ImageView::isMeasurementEdge(
    const measure::CaliperTool& tool,
    const measure::EdgePoint& edge,
    QString* label) const {
    constexpr double matchTolerance = 2.0;

    if (hasPendingEdge_ &&
        pendingEdgeToolId_ == tool.id &&
        std::abs(edge.position - pendingEdgePosition_) <= matchTolerance) {
        if (label) {
            *label = QStringLiteral("A");
        }
        return true;
    }

    for (const auto& measurement : measurements_) {
        if (!measurement.enabled || measurement.caliperToolId != tool.id) {
            continue;
        }
        if (std::abs(edge.position - measurement.edgeAPosition) <= matchTolerance) {
            if (label) {
                *label = QStringLiteral("A");
            }
            return true;
        }
        if (std::abs(edge.position - measurement.edgeBPosition) <= matchTolerance) {
            if (label) {
                *label = QStringLiteral("B");
            }
            return true;
        }
    }

    return false;
}
