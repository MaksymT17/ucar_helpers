#include "powergraph.h"
#include <cmath>
#include <algorithm>
#include <QFont>

PowerGraph::PowerGraph(QWidget *parent) 
    : QWidget(parent), maxHistory(300), maxPowerKw(210.0) {
    setMinimumSize(300, 130);
}

void PowerGraph::addValue(double kw) {
    history.push_back(kw);
    if (history.size() > maxHistory) {
        history.pop_front();
    }
    update(); // Triggers paintEvent
}

void PowerGraph::setMaxKw(double m) {
    maxPowerKw = m;
}

void PowerGraph::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    int w = width();
    int h = height();
    int midY = h / 2;

    // Use a solid line for zero reference to avoid dash artifacts peeking through gaps
    painter.setPen(QPen(QColor(45, 45, 45), 1, Qt::SolidLine));
    painter.drawLine(0, midY, w, midY);

    if (history.empty()) return;

    double xStep = (double)w / maxHistory;

    painter.setPen(Qt::NoPen); // Use fillRect for cleaner, gap-free bars

    for (size_t i = 0; i < history.size(); ++i) {
        double val = history[i];
        double ratio = val / maxPowerKw;
        
        // Ensure that even small auxiliary loads are visible (min 1px)
        int barHeight = static_cast<int>(ratio * (double)midY);
        if (std::abs(val) > 0.01 && barHeight == 0) {
            barHeight = (val > 0) ? 1 : -1;
        }
        
        if (barHeight == 0) continue;

        // Use floating point for positioning to avoid cumulative integer rounding gaps
        // We add a tiny 0.2px overlap to ensure "black limiters" never appear between bars
        double xPos = (double)w - ((double)(history.size() - i) * xStep);
        double barWidth = xStep + 0.2; 
        
        QColor color;
        if (val >= 0) {
            // Consumption: Dynamic Orange to Red
            color = (val > maxPowerKw * 0.8) ? QColor(255, 50, 0) : QColor(255, 140, 0);
            painter.fillRect(QRectF(xPos, midY - barHeight, barWidth, barHeight), color);
        } else {
            // Regen: Electric Green
            color = QColor(0, 255, 150);
            painter.fillRect(QRectF(xPos, midY, barWidth, std::abs(barHeight)), color);
        }
    }
}