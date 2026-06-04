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
    double pendingEdgePosition,
    bool calibrationEnabled,
    double mmPerPixel) {
    const bool imageChanged = image_.cacheKey() != image.cacheKey();
    image_ = image;
    tools_ = tools;
    results_ = results;
    measurements_ = measurements;
    selectedId_ = selectedId;
    hasPendingEdge_ = hasPendingEdge;
    pendingEdgeToolId_ = pendingEdgeToolId;
    pendingEdgePosition_ = pendingEdgePosition;
    calibrationEnabled_ = calibrationEnabled;
    mmPerPixel_ = mmPerPixel;
    if (imageChanged) {
        zoom_ = 1.0;
        pan_ = QPointF(0.0, 0.0);
    }
    update();
}

void ImageView::setCreateMode(bool enabled) {
    createMode_ = enabled ? CreateMode::LineCaliper : CreateMode::None;
    dragMode_ = DragMode::None;
    update();
}

void ImageView::setCreateMode(CreateMode mode) {
    createMode_ = mode;
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

    if (scale >= 8.0) {
        const QPointF visibleTopLeft = widgetToImage(QPointF(0.0, 0.0));
        const QPointF visibleBottomRight = widgetToImage(QPointF(width(), height()));
        const int x0 = std::max(0, static_cast<int>(std::floor(visibleTopLeft.x())));
        const int y0 = std::max(0, static_cast<int>(std::floor(visibleTopLeft.y())));
        const int x1 = std::min(image_.width() - 1, static_cast<int>(std::ceil(visibleBottomRight.x())));
        const int y1 = std::min(image_.height() - 1, static_cast<int>(std::ceil(visibleBottomRight.y())));

        painter.setPen(QPen(QColor(255, 255, 255, 70), 1.0));
        for (int x = x0; x <= x1 + 1; ++x) {
            const double wx = imageOffset().x() + x * scale;
            painter.drawLine(QPointF(wx, imageOffset().y() + y0 * scale),
                             QPointF(wx, imageOffset().y() + (y1 + 1) * scale));
        }
        for (int y = y0; y <= y1 + 1; ++y) {
            const double wy = imageOffset().y() + y * scale;
            painter.drawLine(QPointF(imageOffset().x() + x0 * scale, wy),
                             QPointF(imageOffset().x() + (x1 + 1) * scale, wy));
        }

        if (scale >= 24.0) {
            painter.setPen(QColor(255, 255, 255, 210));
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const int value = qGray(image_.pixel(x, y));
                    const QRectF cell(
                        imageOffset().x() + x * scale,
                        imageOffset().y() + y * scale,
                        scale,
                        scale);
                    painter.drawText(cell, Qt::AlignCenter, QString::number(value));
                }
            }
        }
    }

    for (size_t i = 0; i < tools_.size(); ++i) {
        const auto& tool = tools_[i];
        const bool selected = tool.id == selectedId_;
        if (tool.type == measure::ToolType::TemplateLocator) {
            const QPointF searchTopLeft = imageToWidget({tool.searchRoi.x, tool.searchRoi.y});
            const QPointF searchBottomRight = imageToWidget({
                tool.searchRoi.x + tool.searchRoi.width,
                tool.searchRoi.y + tool.searchRoi.height});
            const QPointF topLeft = imageToWidget({tool.templateRoi.x, tool.templateRoi.y});
            const QPointF bottomRight = imageToWidget({
                tool.templateRoi.x + tool.templateRoi.width,
                tool.templateRoi.y + tool.templateRoi.height});
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(80, 170, 255), selected ? 2.5 : 1.5, Qt::DashLine));
            painter.drawRect(QRectF(searchTopLeft, searchBottomRight).normalized());
            painter.drawText(searchTopLeft + QPointF(4.0, 14.0), QStringLiteral("Search"));
            painter.setPen(QPen(selected ? QColor(255, 215, 80) : QColor(120, 220, 120), selected ? 2.5 : 1.5));
            painter.drawRect(QRectF(topLeft, bottomRight).normalized());
            painter.drawText(topLeft + QPointF(4.0, 14.0), QStringLiteral("Template"));
            if (selected) {
                painter.setBrush(QColor(255, 215, 80));
                painter.drawEllipse(bottomRight, 5.0, 5.0);
                painter.setBrush(QColor(80, 170, 255));
                painter.drawEllipse(searchBottomRight, 5.0, 5.0);
            }
            if (i < results_.size() && results_[i].matchedRoi.width > 0.0 && results_[i].matchedRoi.height > 0.0) {
                const auto& matched = results_[i].matchedRoi;
                const QPointF mt = imageToWidget({matched.x, matched.y});
                const QPointF mb = imageToWidget({matched.x + matched.width, matched.y + matched.height});
                painter.setPen(QPen(QColor(255, 120, 40), 2.0, Qt::DashLine));
                painter.drawRect(QRectF(mt, mb).normalized());
            }
            continue;
        }

        if (tool.type == measure::ToolType::CircleCaliper) {
            const QPointF center = imageToWidget(tool.center);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(selected ? QColor(255, 215, 80) : QColor(80, 210, 255), selected ? 2.5 : 1.5));
            painter.drawEllipse(center, tool.innerRadius * scale, tool.innerRadius * scale);
            painter.drawEllipse(center, tool.outerRadius * scale, tool.outerRadius * scale);
            painter.setBrush(selected ? QColor(255, 215, 80) : QColor(80, 210, 255));
            painter.drawEllipse(center, selected ? 6.0 : 4.0, selected ? 6.0 : 4.0);
            if (i < results_.size()) {
                const auto& result = results_[i];
                painter.setPen(QPen(QColor(255, 70, 70), 1.4));
                painter.setBrush(QColor(255, 70, 70));
                for (const auto& edge : result.circleEdges) {
                    const QPointF ep = imageToWidget(edge.point);
                    painter.drawEllipse(ep, 3.0, 3.0);
                }
                if (result.ok && result.fittedRadius > 0.0) {
                    const QPointF fc = imageToWidget(result.fittedCenter);
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(QColor(255, 255, 255), 2.0, Qt::DashLine));
                    painter.drawEllipse(fc, result.fittedRadius * scale, result.fittedRadius * scale);
                    const QString radiusText = calibrationEnabled_
                        ? QStringLiteral("R=%1 px / %2 mm")
                            .arg(QString::number(result.fittedRadius, 'f', 3))
                            .arg(QString::number(result.fittedRadius * mmPerPixel_, 'f', 6))
                        : QStringLiteral("R=%1 px").arg(QString::number(result.fittedRadius, 'f', 3));
                    painter.setPen(QColor(255, 255, 255));
                    painter.drawText(fc + QPointF(result.fittedRadius * scale + 6.0, -6.0), radiusText);
                }
            }
            continue;
        }

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

    for (const auto& measurement : measurements_) {
        if (!measurement.enabled) {
            continue;
        }
        const measure::EdgePoint* edgeA = nullptr;
        const measure::EdgePoint* edgeB = nullptr;
        for (int i = 0; i < static_cast<int>(tools_.size()) && i < static_cast<int>(results_.size()); ++i) {
            const auto& tool = tools_[static_cast<size_t>(i)];
            const auto& result = results_[static_cast<size_t>(i)];
            for (const auto& edge : result.edges) {
                if (tool.id == measurement.toolAId &&
                    std::abs(edge.position - measurement.edgeAPosition) <= 2.0) {
                    edgeA = &edge;
                }
                if (tool.id == measurement.toolBId &&
                    std::abs(edge.position - measurement.edgeBPosition) <= 2.0) {
                    edgeB = &edge;
                }
            }
        }
        if (!edgeA || !edgeB) {
            continue;
        }
        const QPointF a = imageToWidget(edgeA->point);
        const QPointF b = imageToWidget(edgeB->point);
        const double distancePx = std::hypot(edgeB->point.x - edgeA->point.x, edgeB->point.y - edgeA->point.y);
        painter.setPen(QPen(QColor(255, 255, 255), 1.8, Qt::DashLine));
        painter.drawLine(a, b);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(a, 3.5, 3.5);
        painter.drawEllipse(b, 3.5, 3.5);
        painter.drawText(a + QPointF(5.0, -5.0), QStringLiteral("A"));
        painter.drawText(b + QPointF(5.0, -5.0), QStringLiteral("B"));
        const QString text = calibrationEnabled_
            ? QStringLiteral("%1 px / %2 mm")
                .arg(QString::number(distancePx, 'f', 3))
                .arg(QString::number(distancePx * mmPerPixel_, 'f', 6))
            : QStringLiteral("%1 px").arg(QString::number(distancePx, 'f', 3));
        painter.drawText((a + b) * 0.5 + QPointF(6.0, -6.0), text);
    }

    if (hasMouseImagePoint_) {
        const int x = static_cast<int>(std::round(mouseImagePoint_.x()));
        const int y = static_cast<int>(std::round(mouseImagePoint_.y()));
        if (x >= 0 && x < image_.width() && y >= 0 && y < image_.height()) {
            const int value = qGray(image_.pixel(x, y));
            const QString text = QStringLiteral("x=%1, y=%2, value=%3").arg(x).arg(y).arg(value);
            const QRectF label(8.0, height() - 30.0, 220.0, 22.0);
            painter.fillRect(label, QColor(0, 0, 0, 150));
            painter.setPen(QColor(255, 255, 255));
            painter.drawText(label.adjusted(6.0, 0.0, -4.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, text);
        }
    }

    if (dragMode_ == DragMode::Creating) {
        painter.setPen(QPen(QColor(255, 215, 80), 2.0, Qt::DashLine));
        if (createMode_ == CreateMode::TemplateLocator) {
            painter.drawRect(QRectF(previewP1_, previewP2_).normalized());
        } else if (createMode_ == CreateMode::CircleCaliper) {
            const double radius = std::hypot(previewP2_.x() - previewP1_.x(), previewP2_.y() - previewP1_.y());
            painter.drawEllipse(previewP1_, radius * 0.7, radius * 0.7);
            painter.drawEllipse(previewP1_, radius, radius);
        } else {
            painter.drawLine(previewP1_, previewP2_);
        }
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

    if (createMode_ != CreateMode::None) {
        dragMode_ = DragMode::Creating;
        previewP1_ = event->pos();
        previewP2_ = event->pos();
        return;
    }

    DragMode hitMode = DragMode::None;
    const int hit = findHitTool(event->pos(), &hitMode);
    if (hit >= 0 && (hitMode == DragMode::MoveP1 || hitMode == DragMode::MoveP2 ||
                     hitMode == DragMode::MoveCircleCenter || hitMode == DragMode::MoveCircleInner ||
                     hitMode == DragMode::MoveCircleOuter || hitMode == DragMode::MoveTemplateRoi ||
                     hitMode == DragMode::ResizeTemplateRoi || hitMode == DragMode::MoveSearchRoi ||
                     hitMode == DragMode::ResizeSearchRoi)) {
        dragToolIndex_ = hit;
        dragMode_ = hitMode;
        dragOriginalP1_ = tools_[static_cast<size_t>(hit)].p1;
        dragOriginalP2_ = tools_[static_cast<size_t>(hit)].p2;
        dragOriginalCenter_ = tools_[static_cast<size_t>(hit)].center;
        dragOriginalInnerRadius_ = tools_[static_cast<size_t>(hit)].innerRadius;
        dragOriginalOuterRadius_ = tools_[static_cast<size_t>(hit)].outerRadius;
        const auto& hitTool = tools_[static_cast<size_t>(hit)];
        dragOriginalTemplateRoi_ = QRectF(
            hitTool.templateRoi.x, hitTool.templateRoi.y, hitTool.templateRoi.width, hitTool.templateRoi.height);
        dragOriginalSearchRoi_ = QRectF(
            hitTool.searchRoi.x, hitTool.searchRoi.y, hitTool.searchRoi.width, hitTool.searchRoi.height);
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
        dragOriginalCenter_ = tools_[static_cast<size_t>(hit)].center;
        dragOriginalInnerRadius_ = tools_[static_cast<size_t>(hit)].innerRadius;
        dragOriginalOuterRadius_ = tools_[static_cast<size_t>(hit)].outerRadius;
        const auto& hitTool = tools_[static_cast<size_t>(hit)];
        dragOriginalTemplateRoi_ = QRectF(
            hitTool.templateRoi.x, hitTool.templateRoi.y, hitTool.templateRoi.width, hitTool.templateRoi.height);
        dragOriginalSearchRoi_ = QRectF(
            hitTool.searchRoi.x, hitTool.searchRoi.y, hitTool.searchRoi.width, hitTool.searchRoi.height);
        lastImagePoint_ = widgetToImage(event->pos());
        emit selectedToolChanged(QString::fromStdString(tools_[static_cast<size_t>(hit)].id));
    }
}

void ImageView::mouseMoveEvent(QMouseEvent* event) {
    const QRectF imageRect(imageOffset(), QSizeF(image_.width() * imageScale(), image_.height() * imageScale()));
    hasMouseImagePoint_ = hasImage() && imageRect.contains(event->pos());
    if (hasMouseImagePoint_) {
        mouseImagePoint_ = widgetToImage(event->pos());
    }

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
        if (hit >= 0 && (hitMode == DragMode::MoveP1 || hitMode == DragMode::MoveP2 ||
                         hitMode == DragMode::MoveCircleCenter || hitMode == DragMode::MoveCircleInner ||
                         hitMode == DragMode::MoveCircleOuter || hitMode == DragMode::MoveTemplateRoi ||
                         hitMode == DragMode::ResizeTemplateRoi || hitMode == DragMode::MoveSearchRoi ||
                         hitMode == DragMode::ResizeSearchRoi)) {
            setCursor(Qt::SizeAllCursor);
        } else if (edgeHit.toolIndex >= 0) {
            setCursor(Qt::PointingHandCursor);
        } else if (hit >= 0) {
            setCursor(Qt::SizeAllCursor);
        } else {
            unsetCursor();
        }
        update();
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
    } else if (dragMode_ == DragMode::MoveCircleCenter) {
        tool.center = {imagePoint.x(), imagePoint.y()};
    } else if (dragMode_ == DragMode::MoveCircleInner) {
        tool.innerRadius = std::max(1.0, std::hypot(imagePoint.x() - tool.center.x, imagePoint.y() - tool.center.y));
        if (tool.innerRadius >= tool.outerRadius) {
            tool.outerRadius = tool.innerRadius + 1.0;
        }
    } else if (dragMode_ == DragMode::MoveCircleOuter) {
        tool.outerRadius = std::max(tool.innerRadius + 1.0, std::hypot(imagePoint.x() - tool.center.x, imagePoint.y() - tool.center.y));
    } else if (dragMode_ == DragMode::MoveTemplateRoi || dragMode_ == DragMode::ResizeTemplateRoi ||
               dragMode_ == DragMode::MoveSearchRoi || dragMode_ == DragMode::ResizeSearchRoi) {
        QRectF templateRect = dragOriginalTemplateRoi_;
        QRectF searchRect = dragOriginalSearchRoi_;
        const QPointF delta = imagePoint - lastImagePoint_;
        if (dragMode_ == DragMode::MoveTemplateRoi) {
            templateRect.translate(delta);
            if (!searchRect.contains(templateRect)) {
                templateRect = dragOriginalTemplateRoi_;
            }
        } else if (dragMode_ == DragMode::ResizeTemplateRoi) {
            templateRect.setBottomRight(imagePoint);
            templateRect = templateRect.normalized();
            if (templateRect.width() < 4.0 || templateRect.height() < 4.0 || !searchRect.contains(templateRect)) {
                templateRect = dragOriginalTemplateRoi_;
            }
        } else if (dragMode_ == DragMode::MoveSearchRoi) {
            searchRect.translate(delta);
            if (!searchRect.contains(templateRect)) {
                searchRect = dragOriginalSearchRoi_;
            }
        } else {
            searchRect.setBottomRight(imagePoint);
            searchRect = searchRect.normalized();
            if (searchRect.width() < 4.0 || searchRect.height() < 4.0 || !searchRect.contains(templateRect)) {
                searchRect = dragOriginalSearchRoi_;
            }
        }
        tool.templateRoi = {templateRect.x(), templateRect.y(), templateRect.width(), templateRect.height()};
        tool.searchRoi = {searchRect.x(), searchRect.y(), searchRect.width(), searchRect.height()};
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

    if (tool.type == measure::ToolType::CircleCaliper) {
        emit circleGeometryChanged(
            QString::fromStdString(tool.id),
            QPointF(tool.center.x, tool.center.y),
            tool.innerRadius,
            tool.outerRadius);
    } else if (tool.type == measure::ToolType::TemplateLocator) {
        emit templateGeometryChanged(
            QString::fromStdString(tool.id),
            QRectF(tool.templateRoi.x, tool.templateRoi.y, tool.templateRoi.width, tool.templateRoi.height),
            QRectF(tool.searchRoi.x, tool.searchRoi.y, tool.searchRoi.width, tool.searchRoi.height));
    } else {
        emit toolGeometryChanged(
            QString::fromStdString(tool.id),
            QPointF(tool.p1.x, tool.p1.y),
            QPointF(tool.p2.x, tool.p2.y));
    }
}

void ImageView::mouseReleaseEvent(QMouseEvent* event) {
    if (dragMode_ == DragMode::Creating && event->button() == Qt::LeftButton) {
        const QPointF p1 = widgetToImage(previewP1_);
        const QPointF p2 = widgetToImage(previewP2_);
        if (createMode_ == CreateMode::LineCaliper &&
            std::hypot(p2.x() - p1.x(), p2.y() - p1.y()) >= 5.0) {
            emit caliperCreated(p1, p2);
        } else if (createMode_ == CreateMode::TemplateLocator) {
            const QRectF roi(p1, p2);
            if (std::abs(roi.width()) >= 8.0 && std::abs(roi.height()) >= 8.0) {
                emit templateCreated(roi.normalized());
            }
        } else if (createMode_ == CreateMode::CircleCaliper) {
            const double outerRadius = std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
            if (outerRadius >= 8.0) {
                emit circleCaliperCreated(p1, outerRadius * 0.7, outerRadius);
            }
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
    zoom_ = std::max(0.1, std::min(100.0, zoom_));
    const QPointF afterWidget = imageToWidget({before.x(), before.y()});
    pan_ += event->pos() - afterWidget;
    update();
}

void ImageView::leaveEvent(QEvent*) {
    hasMouseImagePoint_ = false;
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
        const auto& tool = tools_[static_cast<size_t>(i)];
        if (tool.type != measure::ToolType::TemplateLocator) {
            continue;
        }
        const QRectF templateRect(
            imageToWidget({tool.templateRoi.x, tool.templateRoi.y}),
            imageToWidget({tool.templateRoi.x + tool.templateRoi.width, tool.templateRoi.y + tool.templateRoi.height}));
        const QRectF searchRect(
            imageToWidget({tool.searchRoi.x, tool.searchRoi.y}),
            imageToWidget({tool.searchRoi.x + tool.searchRoi.width, tool.searchRoi.y + tool.searchRoi.height}));
        const QPointF templateHandle = templateRect.normalized().bottomRight();
        const QPointF searchHandle = searchRect.normalized().bottomRight();
        const double dt = std::hypot(widgetPoint.x() - templateHandle.x(), widgetPoint.y() - templateHandle.y());
        const double ds = std::hypot(widgetPoint.x() - searchHandle.x(), widgetPoint.y() - searchHandle.y());
        if (dt <= endpointRadius) {
            if (mode) *mode = DragMode::ResizeTemplateRoi;
            return i;
        }
        if (ds <= endpointRadius) {
            if (mode) *mode = DragMode::ResizeSearchRoi;
            return i;
        }
        if (templateRect.normalized().contains(widgetPoint)) {
            if (mode) *mode = DragMode::MoveTemplateRoi;
            return i;
        }
        if (searchRect.normalized().contains(widgetPoint)) {
            if (mode) *mode = DragMode::MoveSearchRoi;
            return i;
        }
    }

    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        const auto& tool = tools_[static_cast<size_t>(i)];
        if (tool.type != measure::ToolType::CircleCaliper) {
            continue;
        }
        const QPointF center = imageToWidget(tool.center);
        const double dCenter = std::hypot(widgetPoint.x() - center.x(), widgetPoint.y() - center.y());
        if (dCenter < bestDistance && dCenter <= endpointRadius) {
            best = i;
            bestDistance = dCenter;
            bestMode = DragMode::MoveCircleCenter;
        }

        const double radius = dCenter;
        const double innerDistance = std::abs(radius - tool.innerRadius * imageScale());
        const double outerDistance = std::abs(radius - tool.outerRadius * imageScale());
        if (innerDistance < bestDistance && innerDistance <= lineRadius) {
            best = i;
            bestDistance = innerDistance;
            bestMode = DragMode::MoveCircleInner;
        }
        if (outerDistance < bestDistance && outerDistance <= lineRadius) {
            best = i;
            bestDistance = outerDistance;
            bestMode = DragMode::MoveCircleOuter;
        }
    }

    for (int i = 0; i < static_cast<int>(tools_.size()); ++i) {
        if (tools_[static_cast<size_t>(i)].type != measure::ToolType::LineCaliper) {
            continue;
        }
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
        if (tools_[static_cast<size_t>(i)].type != measure::ToolType::LineCaliper) {
            continue;
        }
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
        if (tool.type != measure::ToolType::LineCaliper) {
            continue;
        }
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
        if (!measurement.enabled) {
            continue;
        }
        if (measurement.toolAId == tool.id &&
            std::abs(edge.position - measurement.edgeAPosition) <= matchTolerance) {
            if (label) {
                *label = QStringLiteral("A");
            }
            return true;
        }
        if (measurement.toolBId == tool.id &&
            std::abs(edge.position - measurement.edgeBPosition) <= matchTolerance) {
            if (label) {
                *label = QStringLiteral("B");
            }
            return true;
        }
    }

    return false;
}
