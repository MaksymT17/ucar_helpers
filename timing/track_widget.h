#ifndef TRACK_WIDGET_H
#define TRACK_WIDGET_H

#include <QWidget>
#include <QPixmap>
#include <QPainterPath>
#include <QTimer>
#include <vector>
#include <QColor>

struct TelemetryEntry {
    float time;
    QPointF normalizedPos; 
    float speed;
    float distance;
    float throttle;
    float brake;
    int lapNumber;
};

struct TrackConfig {
    QString name;
    QString imagePath;
    std::vector<QPointF> points; // Normalized 0.0 to 1.0
};

struct DriverSimState {
    QString abbreviation;
    std::vector<TelemetryEntry> telemetry;
    float distanceTraveled = 0.0f;
    float lap1FinishDist = 0.0f; // Distance from grid to finish line
    float lastLapTime = 0.0f;
    float lapStartTime = 0.0f;
    int currentLap = 1;
    size_t lastIndex = 0;
    QColor color;
    std::map<int, float> gateCrossingTimes; // GateIndex -> simTime
    int nextGateIndex = 0;
};

class TrackSimulatorWidget : public QWidget {
    Q_OBJECT

public:
    explicit TrackSimulatorWidget(QWidget *parent = nullptr);
    void loadTrack(const TrackConfig& config);
    bool loadTelemetry(const QString& csvPath);

signals:
    void leaderboardUpdated(const QStringList &entries);

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
    QPainterPath scaledPath; // Path scaled to current widget size (full widget)
    
    float simTime = 0.0f;
    size_t maxRevealedIndex = 0;
    float leaderboardTimer = 0.0f;
    std::vector<float> gateDistances; // Distance markers for virtual loops
    bool lapCompleted = false;
    float currentSpeed; 
    float maxSpeed; // Removed const to allow calculation
    std::vector<DriverSimState> drivers;
    bool isDataDriven = false;
    QTimer *animationTimer;
};

#endif // TRACK_WIDGET_H