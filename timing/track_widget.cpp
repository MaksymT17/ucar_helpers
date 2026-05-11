#include "track_widget.h"
#include <QPainter>
#include <QDebug>
#include <QImageReader>
#include <QMouseEvent>
#include <QFile>
#include <QStringList>

MonzaSimWidget::MonzaSimWidget(QWidget *parent) 
    : QWidget(parent), progress(0.0f), currentSpeed(0.0001f) {
    
    // Calculate maxSpeed for a 1:19 (79s) lap
    float targetLapTime = 79.0f;
    float framesPerLap = targetLapTime / 0.016f; // 16ms timer
    // We multiply by 1.25 to account for the speed loss in corners
    maxSpeed = (1.0f / framesPerLap) * 1.25f;

    // Define Monza with precise points for italy.png
    TrackConfig monza;
    monza.name = "Monza Circuit";
    monza.imagePath = ":/italy.png";
    monza.points = {
        { 0.78125 , 0.781667 },
        { 0.755 , 0.785 },
        { 0.41 , 0.778333 },
        { 0.405 , 0.776667 },
        { 0.40625 , 0.763333 },
        { 0.405 , 0.753333 },
        { 0.4 , 0.75 },
        { 0.3775 , 0.761667 },
        { 0.35625 , 0.776667 },
        { 0.33 , 0.781667 },
        { 0.30625 , 0.781667 },
        { 0.2875 , 0.778333 },
        { 0.26 , 0.771667 },
        { 0.22625 , 0.736667 },
        { 0.205 , 0.693333 },
        { 0.195 , 0.66 },
        { 0.1875 , 0.62 },
        { 0.18125 , 0.575 },
        { 0.1775 , 0.521667 },
        { 0.17125 , 0.476667 },
        { 0.16875 , 0.426667 },
        { 0.16125 , 0.346667 },
        { 0.14625 , 0.338333 },
        { 0.1275 , 0.24 },
        { 0.10875 , 0.171667 },
        { 0.10625 , 0.15 },
        { 0.105 , 0.133333 },
        { 0.1075 , 0.115 },
        { 0.11375 , 0.108333 },
        { 0.1225 , 0.0983333 },
        { 0.15875 , 0.0883333 },
        { 0.19 , 0.075 },
        { 0.2075 , 0.0733333 },
        { 0.2125 , 0.0733333 },
        { 0.2225 , 0.0816667 },
        { 0.25125 , 0.156667 },
        { 0.28625 , 0.248333 },
        { 0.29625 , 0.276667 },
        { 0.31 , 0.3 },
        { 0.37375 , 0.41 },
        { 0.435 , 0.518333 },
        { 0.46625 , 0.571667 },
        { 0.47625 , 0.568333 },
        { 0.4875 , 0.568333 },
        { 0.49875 , 0.568333 },
        { 0.5075 , 0.573333 },
        { 0.515 , 0.58 },
        { 0.525 , 0.596667 },
        { 0.53125 , 0.603333 },
        { 0.54375 , 0.605 },
        { 0.62375 , 0.608333 },
        { 0.7875 , 0.615 },
        { 0.88125 , 0.623333 },
        { 0.89375 , 0.638333 },
        { 0.9 , 0.663333 },
        { 0.90125 , 0.69 },
        { 0.89625 , 0.713333 },
        { 0.885 , 0.736667 },
        { 0.87 , 0.753333 },
        { 0.85 , 0.766667 },
        { 0.83 , 0.773333 },
        { 0.80875 , 0.778333 },
        { 0.7825 , 0.78 }
    };

    loadTrack(monza);

    animationTimer = new QTimer(this);
    connect(animationTimer, &QTimer::timeout, this, &MonzaSimWidget::updateAnimation);
    animationTimer->start(16); // ~60 FPS

    setMinimumSize(800, 600);
}

void MonzaSimWidget::loadTrack(const TrackConfig& config) {
    if (!background.load(config.imagePath)) {
        qWarning() << "Failed to load image:" << config.imagePath;
    }

    setupTrackPath(); // Reset internal path
    trackPath = QPainterPath();
    if (!config.points.empty()) {
        trackPath.moveTo(config.points[0]);
        for (size_t i = 1; i < config.points.size(); ++i) {
            trackPath.lineTo(config.points[i]);
        }
        trackPath.lineTo(config.points[0]); // Close the loop
    }
    
    // Refresh scaling
    resizeEvent(nullptr);
    update();
}

bool MonzaSimWidget::loadTelemetry(const QString& csvPath) {
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open telemetry file:" << csvPath;
        return false;
    }

    QTextStream in(&file);
    QString header = in.readLine(); // Skip header
    
    struct RawEntry { float x, y, speed, distance, throttle, brake; };
    std::vector<RawEntry> rawData;
    float minX = 1e10, maxX = -1e10, minY = 1e10, maxY = -1e10;

    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList fields = line.split(',');
        if (fields.size() < 7) continue;

        // CSV format from python: Index, X, Y, Speed, Distance, Throttle, Brake
        RawEntry entry;
        entry.x = fields[1].toFloat();
        entry.y = fields[2].toFloat();
        entry.speed = fields[3].toFloat();
        entry.distance = fields[4].toFloat();
        entry.throttle = fields[5].toFloat();
        entry.brake = fields[6].toFloat();

        minX = std::min(minX, entry.x); maxX = std::max(maxX, entry.x);
        minY = std::min(minY, entry.y); maxY = std::max(maxY, entry.y);
        rawData.push_back(entry);
    }

    if (rawData.empty()) return false;

    // Normalize coordinates and build high-fidelity track path
    telemetryData.clear();
    // trackPath is intentionally NOT redefined here. It should remain the one loaded by loadTrack.
    
    float rangeX = maxX - minX;
    float rangeY = maxY - minY;

    for (size_t i = 0; i < rawData.size(); ++i) {
        // Map telemetry to 0.1 - 0.9 range to keep it off the very edges
        float normX = 0.1f + ((rawData[i].x - minX) / rangeX) * 0.8f;
        float normY = 0.1f + ((rawData[i].y - minY) / rangeY) * 0.8f;

        TelemetryEntry t;
        t.speed = rawData[i].speed;
        t.distance = rawData[i].distance;
        t.throttle = rawData[i].throttle;
        t.brake = rawData[i].brake;
        telemetryData.push_back(t);
    }

    isDataDriven = true;
    progress = 0.0f;
    
    // Max speed for normalization in animation (km/h to progress increment)
    float totalDist = rawData.back().distance;
    // Heuristic: 1 km/h = certain amount of progress per frame
    // This ensures the lap time roughly matches reality
    maxSpeed = (1.0f / totalDist) * (16.0f / 1000.0f); 

    resizeEvent(nullptr);
    update();
    return true;
}

void MonzaSimWidget::setupTrackPath() {
    // Base path initialization is now handled in loadTrack
}

void MonzaSimWidget::resizeEvent(QResizeEvent *event) {
    if (event) QWidget::resizeEvent(event);
    
    // Re-scale the path to fit the new widget size
    QTransform scaler;
    scaler.scale(width(), height());
    scaledPath = scaler.map(trackPath);
}

void MonzaSimWidget::mousePressEvent(QMouseEvent *event) {
    // PRECISION TOOL: Click on the circuit line in the window
    // The coordinates will print to your terminal.
    float normX = (float)event->pos().x() / width();
    float normY = (float)event->pos().y() / height();
    
    qDebug() << "Captured Point: {" << normX << "," << normY << "},";
}

void MonzaSimWidget::updateAnimation() {
    if (trackPath.elementCount() < 2) return;

    if (isDataDriven && !telemetryData.empty()) {
        // Use real telemetry speed to drive progress
        // Find current speed in telemetry (interpolation would be better, but index-based works)
        int index = std::clamp(int(progress * (telemetryData.size() - 1)), 0, (int)telemetryData.size() - 1);
        float kmh = telemetryData[index].speed;
        
        // Convert km/h to "progress per 16ms frame"
        // 1 km/h = 0.277 m/s. In 16ms, that's 0.0044 meters.
        float mPerFrame = kmh * 0.2777f * 0.016f;
        float totalTrackMeters = telemetryData.back().distance;
        
        progress += (mPerFrame / totalTrackMeters);
        if (progress > 1.0f) progress -= 1.0f;
        update();
        return;
    }

    // 1. Look ahead to detect corners (sample 3% of the track ahead)
    float lookAheadProgress = std::fmod(progress + 0.03f, 1.0f);
    
    // 2. Calculate direction change (Curvature)
    // QPainterPath::angleAtPercent returns the tangent angle
    float currentAngle = trackPath.angleAtPercent(progress);
    float futureAngle  = trackPath.angleAtPercent(lookAheadProgress);
    
    float angleDiff = std::abs(futureAngle - currentAngle);
    if (angleDiff > 180.0f) angleDiff = 360.0f - angleDiff; // Handle 360/0 crossover

    // 3. Determine Target Speed
    // If angleDiff is high (sharp corner), target speed is low.
    // Heuristic: reduce speed by up to 70% in sharp turns
    float curvatureFactor = std::clamp(angleDiff / 45.0f, 0.0f, 1.0f); 
    float targetSpeed = maxSpeed * (1.0f - (curvatureFactor * 0.75f));

    // 4. Smooth Acceleration/Braking
    float lerpFactor = (targetSpeed < currentSpeed) ? 0.15f : 0.03f; // Brake 5x faster than accel
    currentSpeed += (targetSpeed - currentSpeed) * lerpFactor;

    progress += currentSpeed;
    if (progress > 1.0f) progress -= 1.0f;
    update(); // Redraw
}

void MonzaSimWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 1. Draw Background
    painter.fillRect(rect(), Qt::black); 
    if (!background.isNull()) {
        painter.drawPixmap(rect(), background);
    } else {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "Track Image Not Found\nCheck Terminal for Debug Info");
    }

    // 2. Optional: Draw Track Line (for debugging/visual alignment) - Commented out to avoid confusion
    // painter.setPen(QPen(QColor(255, 255, 255, 100), 2, Qt::DashLine));
    // painter.drawPath(scaledPath);

    // 3. Calculate Car Position
    // pointAtPercent is very powerful for track simulations
    QPointF carPos = scaledPath.pointAtPercent(progress);

    // 4. Draw the Car (Dot)
    painter.setBrush(Qt::red);
    painter.setPen(QPen(Qt::white, 2));
    painter.drawEllipse(carPos, 10, 10);
    
    // 5. Draw Info
    int dataIdx = isDataDriven ? std::clamp(int(progress * (telemetryData.size() - 1)), 0, (int)telemetryData.size() - 1) : -1;

    int textX = width() - 350; // Position text on the right side
    painter.setPen(Qt::yellow);
    painter.drawText(textX, 30, QString("Simulating: Monza Circuit %1").arg(isDataDriven ? "(REAL DATA)" : ""));
    painter.drawText(textX, 50, QString("Lap Progress: %1%").arg(int(progress * 100)));

    if (isDataDriven && dataIdx >= 0) {
        float speed = telemetryData[dataIdx].speed;
        float thr = telemetryData[dataIdx].throttle;
        float brk = telemetryData[dataIdx].brake;
        painter.drawText(textX, 70, QString("Speed: %1 km/h").arg(speed, 0, 'f', 1));
        painter.setPen(thr > 0 ? Qt::green : (brk > 0 ? Qt::red : Qt::white));
        painter.drawText(textX, 90, QString("Input: %1").arg(thr > 0 ? "THROTTLE" : (brk > 0 ? "BRAKING" : "COASTING")));
    }
}