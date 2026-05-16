#include "track_widget.h"
#include <QPainter>
#include <QDebug>
#include <QImageReader>
#include <QMouseEvent>
#include <QFile>
#include <QStringList>
#include <QFileInfo>

TrackSimulatorWidget::TrackSimulatorWidget(QWidget *parent) 
    : QWidget(parent), currentSpeed(0.0001f) {
    
    // Default maxSpeed for heuristic mode
    float targetLapTime = 79.0f;
    float framesPerLap = targetLapTime / 0.016f; // 16ms timer
    // We multiply by 1.25 to account for the speed loss in corners
    maxSpeed = (1.0f / framesPerLap) * 1.25f;

    animationTimer = new QTimer(this);
    connect(animationTimer, &QTimer::timeout, this, &TrackSimulatorWidget::updateAnimation);
    animationTimer->start(16); // ~60 FPS

    setMinimumSize(800, 600);
}

void TrackSimulatorWidget::loadTrack(const TrackConfig& config) {
    // The background image is no longer loaded or used for drawing the track.
    // Note: trackPath is now built dynamically in loadTelemetry from actual data
    // but we reset it here just in case.
    trackPath = QPainterPath(); 

    resizeEvent(nullptr);
    update();
}

bool TrackSimulatorWidget::loadTelemetry(const QString& csvPath) {
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Could not open telemetry file:" << csvPath;
        return false;
    }

    // Better parsing for driver abbreviation (e.g., australia_2026_VER_telemetry.csv)
    QString fileName = QFileInfo(csvPath).fileName();
    QStringList parts = fileName.split('_');
    QString abb = "UNK";
    for(const QString& p : parts) if(p.length() == 3 && p == p.toUpper()) abb = p;
    if(abb == "UNK" && parts.size() >= 3) abb = parts[2]; 

    QTextStream in(&file);
    QString header = in.readLine(); // Skip header
    
    struct RawEntry { float time, x, y, speed, distance, throttle, brake; };
    std::vector<RawEntry> rawData;
    float minX = 1e10, maxX = -1e10, minY = 1e10, maxY = -1e10;

    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList fields = line.split(',');
        if (fields.size() < 8) continue;

        // CSV format: Index, TimeSeconds, X, Y, Speed, Distance, Throttle, Brake
        RawEntry entry;
        entry.time = fields[1].toFloat();
        entry.x = fields[2].toFloat();
        entry.y = fields[3].toFloat();
        entry.speed = fields[4].toFloat();
        entry.distance = fields[5].toFloat();
        entry.throttle = fields[6].toFloat();
        entry.brake = fields[7].toFloat();

        // Filter out (0,0) outliers which are common in garage/pit telemetry
        // These distort the scaling and create the "lines across the middle"
        if (std::abs(entry.x) < 1.0f && std::abs(entry.y) < 1.0f) {
            continue;
        }

        minX = std::min(minX, entry.x); maxX = std::max(maxX, entry.x);
        minY = std::min(minY, entry.y); maxY = std::max(maxY, entry.y);
        rawData.push_back(entry);
    }

    if (rawData.empty()) return false;

    DriverSimState newDriver;
    newDriver.abbreviation = abb;
    // Assign a color based on the number of drivers already loaded
    newDriver.color = QColor::fromHsv((drivers.size() * 40) % 360, 200, 255);
    
    float rangeX = maxX - minX;
    float rangeY = maxY - minY;
    float maxRange = std::max(rangeX, rangeY);
    float centerX = (minX + maxX) / 2.0f;
    float centerY = (minY + maxY) / 2.0f;

    for (size_t i = 0; i < rawData.size(); ++i) {
        // 1. Normalize relative to center and preserve aspect ratio
        float nx = (rawData[i].x - centerX) / maxRange;
        float ny = (rawData[i].y - centerY) / maxRange;

        // 2. ROTATION & ALIGNMENT: To fix vertical lines and show telemetry
        // as a horizontal track, we map telemetry Y to screen X and telemetry X to screen Y.
        // We also invert the Y-axis to match screen coordinates (Y increases downwards).
        float finalX = 0.5f + ny * 0.9f;  // Telemetry Y (vertical component) maps to screen X (horizontal)
        float finalY = 0.5f - nx * 0.9f;  // Telemetry X (horizontal component) maps to screen Y (vertical, inverted)

        TelemetryEntry t;
        t.time = rawData[i].time;
        t.normalizedPos = QPointF(finalX, finalY);
        t.speed = rawData[i].speed;
        t.distance = rawData[i].distance;
        t.throttle = rawData[i].throttle;
        t.brake = rawData[i].brake;
        newDriver.telemetry.push_back(t);
    }

    // Start exactly at the beginning of the race telemetry (the Grid)
    newDriver.distanceTraveled = newDriver.telemetry.front().distance;
    newDriver.lastIndex = 0;

    maxRevealedIndex = 0;
    lapCompleted = false;

    // Initialize global simTime to 0.0 (the normalized start of Lap 1)
    if (drivers.empty() && !newDriver.telemetry.empty()) {
        simTime = 0.0f;
    }

    // 3. GENERATE TRACK PATH FROM FIRST DRIVER
    if (drivers.empty()) {
        trackPath = QPainterPath();
        if (newDriver.telemetry.size() > 1) {
            trackPath.moveTo(newDriver.telemetry[0].normalizedPos);
            for (size_t i = 1; i < newDriver.telemetry.size(); ++i) {
                trackPath.lineTo(newDriver.telemetry[i].normalizedPos);
            }
        }
    }

    drivers.push_back(newDriver);
    isDataDriven = true;
    qDebug() << "Loaded" << newDriver.telemetry.size() << "telemetry entries for" << newDriver.abbreviation;
    if (!newDriver.telemetry.empty()) qDebug() << "First speed:" << newDriver.telemetry.front().speed << "Last distance:" << newDriver.telemetry.back().distance;
    
    resizeEvent(nullptr);
    update();
    return true;
}

void TrackSimulatorWidget::setupTrackPath() {
    // Base path initialization is now handled in loadTrack
}

void TrackSimulatorWidget::resizeEvent(QResizeEvent *event) {
    if (event) QWidget::resizeEvent(event);
    
    // Map the normalized path (0.0 - 1.0) directly to the widget's full size
    // The track will now fill the available widget space, maintaining its aspect ratio
    // as determined by the normalization in loadTelemetry.
    QTransform scaler;
    scaler.translate(0, 0); // Draw from top-left of the widget
    scaler.scale(width(), height()); // Scale to the full width and height of the widget
    scaledPath = scaler.map(trackPath);
}

void TrackSimulatorWidget::mousePressEvent(QMouseEvent *event) {
    // PRECISION TOOL: Click on the circuit line in the window
    // The coordinates will print to your terminal.
    // Normalize click position relative to the widget's full dimensions
    float normX = (float)event->pos().x() / width();
    float normY = (float)event->pos().y() / height();
    
    qDebug() << "Captured Point: {" << normX << "," << normY << "},";
}

void TrackSimulatorWidget::updateAnimation() {
    if (trackPath.elementCount() < 2) return;

    if (isDataDriven && !drivers.empty()) {
        simTime += 0.016f; // Advance session time by 16ms

        for (auto& driver : drivers) {
            if (driver.telemetry.empty()) continue;

            // Use session timestamps to find the current position
            while (driver.lastIndex < driver.telemetry.size() - 1 &&
                   driver.telemetry[driver.lastIndex].time < simTime) {
                driver.lastIndex++;
            }

            // Track the furthest point reached across all drivers
            if (driver.lastIndex > maxRevealedIndex) {
                maxRevealedIndex = driver.lastIndex;
            }

            // Keep distanceTraveled synced for any distance-based logic
            driver.distanceTraveled = driver.telemetry[driver.lastIndex].distance;

            // Reset if we reach the end of the race data
            if (driver.lastIndex >= driver.telemetry.size() - 1) 
            {
                lapCompleted = true; // Once a lap is done, keep the whole line visible
                simTime = driver.telemetry.front().time;
                for(auto& d : drivers) d.lastIndex = 0;
            }        }
        update();
        return;
    }

    // Fallback heuristic for a single ghost car if no data is loaded
    static float progress = 0.0f;
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

void TrackSimulatorWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 1. Draw Background
    // The background image is no longer used. Fill the entire widget with black.
    painter.fillRect(rect(), Qt::black);

    // 2. Draw Track Line (Reveal logic)
    if (!drivers.empty()) {
        const auto& refTelemetry = drivers[0].telemetry;
        size_t limit = lapCompleted ? refTelemetry.size() : maxRevealedIndex;

        if (limit > 1) {
            painter.setPen(QPen(QColor(255, 255, 255, 80), 2));
            QPainterPath revealPath;
            
            QPointF startPoint = refTelemetry[0].normalizedPos;
            revealPath.moveTo(startPoint.x() * width(), startPoint.y() * height());

            for (size_t i = 1; i < limit; ++i) {
                QPointF p = refTelemetry[i].normalizedPos;
                revealPath.lineTo(p.x() * width(), p.y() * height());
            }
            painter.drawPath(revealPath);
        }
    }

    // 3. Draw All Drivers
    for (const auto& driver : drivers) {
        if (driver.telemetry.empty()) continue;

        size_t index = driver.lastIndex;

        // Map the stored telemetry point to the current widget scaling
        QPointF normPos = driver.telemetry[index].normalizedPos;
        // Scale normalizedPos (0-1) directly to widget coordinates
        QPointF carPos = QPointF(normPos.x() * width(), normPos.y() * height());
        
        painter.setBrush(driver.color);
        painter.setPen(QPen(Qt::white, 1));
        painter.drawEllipse(carPos, 10, 10);
        
        // Draw Label
        painter.drawText(carPos + QPointF(12, 5), driver.abbreviation);
    }

    // Basic Info Overlay
    painter.setPen(Qt::yellow);
    painter.drawText(20, 30, "Australia GP 2026 Visualization");
    if (drivers.empty()) {
        painter.drawText(20, 50, "Waiting for telemetry data...");
    }
}