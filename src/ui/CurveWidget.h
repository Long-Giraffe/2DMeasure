#pragma once

#include <QWidget>

#include <vector>

class CurveWidget : public QWidget {
    Q_OBJECT

public:
    enum class DisplayMode {
        Profile,
        Gradient,
        Both,
    };

    explicit CurveWidget(QWidget* parent = nullptr);

    void setData(
        const std::vector<float>& profile,
        const std::vector<float>& gradient,
        double xStart,
        double xStep,
        double positiveThreshold,
        double negativeThreshold);
    void setDisplayMode(DisplayMode mode);
    void setThresholds(double positiveThreshold, double negativeThreshold);

signals:
    void thresholdsChanged(double positiveThreshold, double negativeThreshold);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class DragTarget {
        None,
        Positive,
        Negative,
    };

    QRect plotRect() const;
    double valueToY(double value, double minValue, double maxValue) const;
    double yToValue(double y, double minValue, double maxValue) const;
    void valueRange(double* minValue, double* maxValue) const;
    DragTarget hitThresholdLine(const QPoint& pos) const;

    std::vector<float> profile_;
    std::vector<float> gradient_;
    double xStart_ = 0.0;
    double xStep_ = 1.0;
    double positiveThreshold_ = 8.0;
    double negativeThreshold_ = -8.0;
    DisplayMode displayMode_ = DisplayMode::Both;
    DragTarget dragTarget_ = DragTarget::None;
    DragTarget hoverTarget_ = DragTarget::None;
    QPoint mousePos_;
    bool hasMousePos_ = false;
};
