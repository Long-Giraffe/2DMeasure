#include "ui/CurveWidget.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

CurveWidget::CurveWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(220);
    setMouseTracking(true);
}

void CurveWidget::setData(
    const std::vector<float>& profile,
    const std::vector<float>& gradient,
    double xStart,
    double xStep,
    double positiveThreshold,
    double negativeThreshold) {
    profile_ = profile;
    gradient_ = gradient;
    xStart_ = xStart;
    xStep_ = std::max(1e-9, xStep);
    positiveThreshold_ = positiveThreshold;
    negativeThreshold_ = negativeThreshold;
    update();
}

void CurveWidget::setDisplayMode(DisplayMode mode) {
    displayMode_ = mode;
    update();
}

void CurveWidget::setThresholds(double positiveThreshold, double negativeThreshold) {
    positiveThreshold_ = positiveThreshold;
    negativeThreshold_ = negativeThreshold;
    update();
}

void CurveWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(246, 248, 250));

    const QRect plot = plotRect();
    if (profile_.empty() && gradient_.empty()) {
        painter.setPen(QColor(120, 120, 120));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("No curve"));
        return;
    }

    double minValue = 0.0;
    double maxValue = 1.0;
    valueRange(&minValue, &maxValue);

    painter.setFont(QFont(painter.font().family(), 8));
    painter.setPen(QPen(QColor(225, 230, 235), 1.0));
    for (int i = 0; i <= 5; ++i) {
        const double ratio = static_cast<double>(i) / 5.0;
        const int x = plot.left() + static_cast<int>(std::round(ratio * plot.width()));
        painter.drawLine(x, plot.top(), x, plot.bottom());
    }
    for (int i = 0; i <= 4; ++i) {
        const double ratio = static_cast<double>(i) / 4.0;
        const int y = plot.top() + static_cast<int>(std::round(ratio * plot.height()));
        painter.drawLine(plot.left(), y, plot.right(), y);
    }

    painter.setPen(QPen(QColor(145, 155, 165), 1.0));
    painter.drawRect(plot);

    const size_t xCount = std::max(profile_.size(), gradient_.size());
    const double xEnd = xStart_ + (xCount > 1 ? static_cast<double>(xCount - 1) * xStep_ : 0.0);
    painter.setPen(QColor(75, 85, 95));
    for (int i = 0; i <= 5; ++i) {
        const double ratio = static_cast<double>(i) / 5.0;
        const int x = plot.left() + static_cast<int>(std::round(ratio * plot.width()));
        const double value = xStart_ + (xEnd - xStart_) * ratio;
        painter.drawLine(x, plot.bottom(), x, plot.bottom() + 4);
        painter.drawText(
            QRectF(x - 34, plot.bottom() + 6, 68, 18),
            Qt::AlignCenter,
            QString::number(value, 'f', 1));
    }
    for (int i = 0; i <= 4; ++i) {
        const double ratio = static_cast<double>(i) / 4.0;
        const int y = plot.bottom() - static_cast<int>(std::round(ratio * plot.height()));
        const double value = minValue + (maxValue - minValue) * ratio;
        painter.drawLine(plot.left() - 4, y, plot.left(), y);
        painter.drawText(
            QRectF(2, y - 9, plot.left() - 8, 18),
            Qt::AlignRight | Qt::AlignVCenter,
            QString::number(value, 'f', 1));
    }
    painter.drawText(QRectF(plot.left(), height() - 18, plot.width(), 16), Qt::AlignCenter, QStringLiteral("x(px)"));

    auto drawCurve = [&](const std::vector<float>& values, const QColor& color) {
        if (values.size() < 2) {
            return;
        }
        QPainterPath path;
        for (size_t i = 0; i < values.size(); ++i) {
            const double x = plot.left() + static_cast<double>(i) * plot.width() /
                                           static_cast<double>(values.size() - 1);
            const double y = valueToY(values[i], minValue, maxValue);
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                path.lineTo(x, y);
            }
        }
        painter.setPen(QPen(color, 1.7));
        painter.drawPath(path);
    };

    painter.save();
    painter.setClipRect(plot.adjusted(1, 1, -1, -1));
    if (displayMode_ == DisplayMode::Profile || displayMode_ == DisplayMode::Both) {
        drawCurve(profile_, QColor(70, 70, 70));
    }
    if (displayMode_ == DisplayMode::Gradient || displayMode_ == DisplayMode::Both) {
        drawCurve(gradient_, QColor(20, 110, 220));
    }
    painter.restore();

    if (displayMode_ == DisplayMode::Gradient || displayMode_ == DisplayMode::Both) {
        auto drawThreshold = [&](double threshold, const QColor& color, const QString& text, DragTarget target) {
            const double y = valueToY(threshold, minValue, maxValue);
            const bool active = dragTarget_ == target || hoverTarget_ == target;
            painter.setPen(QPen(color, active ? 4.0 : 3.0));
            painter.drawLine(plot.left(), static_cast<int>(std::round(y)), plot.right(), static_cast<int>(std::round(y)));

            const QRectF handle(plot.right() - 78, y - 12, 74, 24);
            painter.setBrush(active ? QColor(255, 255, 255) : QColor(248, 248, 248));
            painter.setPen(QPen(color, 2.0));
            painter.drawRoundedRect(handle, 5.0, 5.0);
            painter.drawText(handle, Qt::AlignCenter, text + QStringLiteral(" ") + QString::number(threshold, 'f', 1));
        };

        drawThreshold(positiveThreshold_, QColor(210, 45, 45), QStringLiteral("+T"), DragTarget::Positive);
        drawThreshold(negativeThreshold_, QColor(15, 145, 90), QStringLiteral("-T"), DragTarget::Negative);
    }

    painter.setPen(QColor(55, 65, 75));
    painter.drawText(plot.left(), 16, QStringLiteral("Profile"));
    painter.setPen(QColor(20, 110, 220));
    painter.drawText(plot.left() + 58, 16, QStringLiteral("Gradient"));

    if (hasMousePos_ && plot.contains(mousePos_)) {
        const double xr = static_cast<double>(mousePos_.x() - plot.left()) /
                          std::max(1.0, static_cast<double>(plot.width()));
        const double xValue = xStart_ + std::clamp(xr, 0.0, 1.0) * (xEnd - xStart_);
        const double yValue = yToValue(mousePos_.y(), minValue, maxValue);
        painter.setPen(QPen(QColor(90, 100, 110), 1.0, Qt::DashLine));
        painter.drawLine(mousePos_.x(), plot.top(), mousePos_.x(), plot.bottom());

        const QString text = QStringLiteral("x=%1  y=%2")
            .arg(QString::number(xValue, 'f', 2))
            .arg(QString::number(yValue, 'f', 2));
        const QRectF label(mousePos_ + QPoint(10, -28), QSizeF(120, 22));
        painter.setBrush(QColor(255, 255, 255, 235));
        painter.setPen(QPen(QColor(160, 165, 170), 1.0));
        painter.drawRoundedRect(label, 4.0, 4.0);
        painter.setPen(QColor(45, 55, 65));
        painter.drawText(label, Qt::AlignCenter, text);
    }
}

void CurveWidget::mousePressEvent(QMouseEvent* event) {
    dragTarget_ = hitThresholdLine(event->pos());
    if (dragTarget_ != DragTarget::None) {
        setCursor(Qt::SizeVerCursor);
    }
}

void CurveWidget::mouseMoveEvent(QMouseEvent* event) {
    mousePos_ = event->pos();
    hasMousePos_ = true;

    if (dragTarget_ == DragTarget::None) {
        hoverTarget_ = hitThresholdLine(event->pos());
        if (hoverTarget_ == DragTarget::None) {
            unsetCursor();
        } else {
            setCursor(Qt::SizeVerCursor);
        }
        update();
        return;
    }

    double minValue = 0.0;
    double maxValue = 1.0;
    valueRange(&minValue, &maxValue);
    const double value = yToValue(event->pos().y(), minValue, maxValue);
    if (dragTarget_ == DragTarget::Positive) {
        positiveThreshold_ = std::max(0.0, value);
    } else {
        negativeThreshold_ = std::min(0.0, value);
    }
    emit thresholdsChanged(positiveThreshold_, negativeThreshold_);
    update();
}

void CurveWidget::mouseReleaseEvent(QMouseEvent*) {
    dragTarget_ = DragTarget::None;
    unsetCursor();
    update();
}

void CurveWidget::leaveEvent(QEvent*) {
    hasMousePos_ = false;
    hoverTarget_ = DragTarget::None;
    unsetCursor();
    update();
}

QRect CurveWidget::plotRect() const {
    return rect().adjusted(58, 28, -28, -44);
}

double CurveWidget::valueToY(double value, double minValue, double maxValue) const {
    const QRect plot = plotRect();
    const double ratio = (value - minValue) / std::max(1e-9, maxValue - minValue);
    return plot.bottom() - ratio * plot.height();
}

double CurveWidget::yToValue(double y, double minValue, double maxValue) const {
    const QRect plot = plotRect();
    const double ratio = (plot.bottom() - y) / std::max(1.0, static_cast<double>(plot.height()));
    return minValue + ratio * (maxValue - minValue);
}

void CurveWidget::valueRange(double* minValue, double* maxValue) const {
    *minValue = 0.0;
    *maxValue = 1.0;

    auto includeValues = [&](const std::vector<float>& values) {
        for (float value : values) {
            *minValue = std::min(*minValue, static_cast<double>(value));
            *maxValue = std::max(*maxValue, static_cast<double>(value));
        }
    };

    if (displayMode_ == DisplayMode::Profile || displayMode_ == DisplayMode::Both) {
        includeValues(profile_);
    }
    if (displayMode_ == DisplayMode::Gradient || displayMode_ == DisplayMode::Both) {
        includeValues(gradient_);
        *minValue = std::min(*minValue, negativeThreshold_);
        *maxValue = std::max(*maxValue, positiveThreshold_);
    }

    if (std::abs(*maxValue - *minValue) < 1e-9) {
        *minValue -= 1.0;
        *maxValue += 1.0;
    } else {
        const double pad = (*maxValue - *minValue) * 0.08;
        *minValue -= pad;
        *maxValue += pad;
    }
}

CurveWidget::DragTarget CurveWidget::hitThresholdLine(const QPoint& pos) const {
    if (displayMode_ == DisplayMode::Profile) {
        return DragTarget::None;
    }

    const QRect plot = plotRect();
    if (!plot.adjusted(-8, -18, 8, 18).contains(pos)) {
        return DragTarget::None;
    }

    double minValue = 0.0;
    double maxValue = 1.0;
    valueRange(&minValue, &maxValue);
    const double posY = valueToY(positiveThreshold_, minValue, maxValue);
    const double negY = valueToY(negativeThreshold_, minValue, maxValue);

    const double dPos = std::abs(pos.y() - posY);
    const double dNeg = std::abs(pos.y() - negY);
    if (dPos <= 12.0 && dPos <= dNeg) {
        return DragTarget::Positive;
    }
    if (dNeg <= 12.0) {
        return DragTarget::Negative;
    }
    return DragTarget::None;
}
