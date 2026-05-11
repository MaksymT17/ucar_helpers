#ifndef TRACK_WIDGET_H
#define TRACK_WIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QPainterPath>
#include <QTimer>
#include <vector>

struct TelemetryEntry {
    // QPointF normalizedPos; // Removed as car position is now driven by progress along the main trackPath
    float speed;
    float distance;
    float throttle;
    float brake;
};

struct TrackConfig {
    QString name;
    QString imagePath;
    std::vector<QPointF> points; // Normalized 0.0 to 1.0
};

class MonzaSimWidget : public QWidget {
    Q_OBJECT

public:
    explicit MonzaSimWidget(QWidget *parent = nullptr);
    void loadTrack(const TrackConfig& config);
    bool loadTelemetry(const QString& csvPath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override; // Utility to capture coordinates

private slots:
    void updateAnimation();

private:
    void setupTrackPath();
    
    QPixmap background;
    QPainterPath trackPath;
    QPainterPath scaledPath; // Path scaled to current widget size
    
    float progress; // 0.0 to 1.0
    float currentSpeed; 
    float maxSpeed; // Removed const to allow calculation
    std::vector<TelemetryEntry> telemetryData;
    bool isDataDriven = false;
    QTimer *animationTimer;
};

#endif // TRACK_WIDGET_H