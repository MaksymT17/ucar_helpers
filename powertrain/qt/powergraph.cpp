#include "powergraph.h"

PowerGraph::PowerGraph(QWidget *parent) 
    : QWidget(parent), maxHistory(300), maxPowerKw(210.0) {
    setMinimumSize(300, 100);
    history.resize(maxHistory, 0.0);
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

    // Draw subtle zero reference line
    painter.setPen(QPen(QColor(60, 60, 60), 1, Qt::DashLine));
    painter.drawLine(0, midY, w, midY);

    if (history.empty()) return;

    double xStep = (double)w / maxHistory;

    for (size_t i = 0; i < history.size(); ++i) {
        double val = history[i];
        double ratio = val / maxPowerKw;
        int barHeight = static_cast<int>(ratio * midY);
        int x = static_cast<int>(i * xStep);
        
        if (val >= 0) {
            // Consumption: Dynamic Orange to Red
            QColor color = (val > maxPowerKw * 0.8) ? QColor(255, 50, 0) : QColor(255, 140, 0);
            painter.setPen(color);
            painter.drawLine(x, midY, x, midY - barHeight);
        } else {
            // Regen: Electric Green
            painter.setPen(QColor(0, 255, 150));
            painter.drawLine(x, midY, x, midY - barHeight);
        }
    }
}