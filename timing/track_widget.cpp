#include "track_widget.h"
#include <QPainter>
#include <QDebug>
#include <QImageReader>
#include <QMouseEvent>
#include <QFile>
#include <QStringList>
#include <QFileInfo>

MonzaSimWidget::MonzaSimWidget(QWidget *parent) 
    : QWidget(parent), currentSpeed(0.0001f) {
    
    // Default maxSpeed for heuristic mode
    float targetLapTime = 79.0f;
    float framesPerLap = targetLapTime / 0.016f; // 16ms timer
    // We multiply by 1.25 to account for the speed loss in corners
    maxSpeed = (1.0f / framesPerLap) * 1.25f;

    // Define Australia with precise points for australia.png
    TrackConfig australia;
    australia.name = "Albert Park Circuit";
    australia.imagePath = "australia.png";
    australia.points = {
        { 0.57125 , 0.88 },
        { 0.565 , 0.88 },
        { 0.4225 , 0.883333 },
        { 0.4 , 0.886667 },
        { 0.39375 , 0.881667 },
        { 0.3925 , 0.873333 },
        { 0.38875 , 0.853333 },
        { 0.385 , 0.838333 },
        { 0.37625 , 0.823333 },
        { 0.36375 , 0.811667 },
        { 0.35125 , 0.803333 },
        { 0.345 , 0.801667 },
        { 0.3125 , 0.806667 },
        { 0.27625 , 0.808333 },
        { 0.24625 , 0.805 },
        { 0.21375 , 0.801667 },
        { 0.1825 , 0.791667 },
        { 0.135 , 0.775 },
        { 0.10375 , 0.76 },
        { 0.09625 , 0.75 },
        { 0.09625 , 0.738333 },
        { 0.1075 , 0.71 },
        { 0.12625 , 0.663333 },
        { 0.12625 , 0.651667 },
        { 0.12625 , 0.64 },
        { 0.08625 , 0.58 },
        { 0.06125 , 0.536667 },
        { 0.05625 , 0.518333 },
        { 0.05625 , 0.498333 },
        { 0.06625 , 0.396667 },
        { 0.06625 , 0.38 },
        { 0.07125 , 0.355 },
        { 0.08125 , 0.323333 },
        { 0.09 , 0.28 },
        { 0.0975 , 0.225 },
        { 0.10125 , 0.201667 },
        { 0.11125 , 0.195 },
        { 0.13375 , 0.188333 },
        { 0.14625 , 0.176667 },
        { 0.165 , 0.156667 },
        { 0.18875 , 0.126667 },
        { 0.2125 , 0.115 },
        { 0.23375 , 0.113333 },
        { 0.24875 , 0.116667 },
        { 0.265 , 0.128333 },
        { 0.28875 , 0.153333 },
        { 0.31875 , 0.198333 },
        { 0.335 , 0.22 },
        { 0.3475 , 0.246667 },
        { 0.365 , 0.298333 },
        { 0.38375 , 0.375 },
        { 0.4 , 0.428333 },
        { 0.42125 , 0.473333 },
        { 0.4425 , 0.501667 },
        { 0.46375 , 0.521667 },
        { 0.49375 , 0.54 },
        { 0.5225 , 0.548333 },
        { 0.60125 , 0.54 },
        { 0.615 , 0.533333 },
        { 0.62 , 0.52 },
        { 0.6475 , 0.461667 },
        { 0.6575 , 0.456667 },
        { 0.71625 , 0.445 },
        { 0.7675 , 0.44 },
        { 0.7875 , 0.446667 },
        { 0.81 , 0.456667 },
        { 0.84625 , 0.486667 },
        { 0.91875 , 0.555 },
        { 0.935 , 0.571667 },
        { 0.94375 , 0.59 },
        { 0.945 , 0.596667 },
        { 0.93375 , 0.636667 },
        { 0.91875 , 0.683333 },
        { 0.90375 , 0.733333 },
        { 0.895 , 0.763333 },
        { 0.88625 , 0.771667 },
        { 0.87375 , 0.776667 },
        { 0.84375 , 0.768333 },
        { 0.81125 , 0.756667 },
        { 0.785 , 0.748333 },
        { 0.78 , 0.751667 },
        { 0.77625 , 0.766667 },
        { 0.77875 , 0.8 },
        { 0.77625 , 0.835 },
        { 0.775 , 0.853333 },
        { 0.76875 , 0.866667 },
        { 0.7525 , 0.875 },
        { 0.6325 , 0.878333 },
        { 0.5875 , 0.881667 }
    };

    loadTrack(australia);

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

    // Extract driver abbreviation from filename (e.g., australia_2026_ANT_telemetry.csv)
    QString fileName = QFileInfo(csvPath).fileName();
    QStringList parts = fileName.split('_');
    QString abb = (parts.size() >= 3) ? parts[2] : "UNK";

    QTextStream in(&file);
    QString header = in.readLine(); // Skip header
    
    struct RawEntry { float x, y, speed, distance, throttle, brake; };
    std::vector<RawEntry> rawData;
    float minX = 1e10, maxX = -1e10, minY = 1e10, maxY = -1e10;

    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList fields = line.split(',');
        if (fields.size() < 7) continue;

        // CSV format: Index, X, Y, Speed, Distance, Throttle, Brake
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

    DriverSimState newDriver;
    newDriver.abbreviation = abb;
    // Assign a color based on the number of drivers already loaded
    newDriver.color = QColor::fromHsv((drivers.size() * 40) % 360, 200, 255);

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
        newDriver.telemetry.push_back(t);
    }

    // Optimization: Skip the "sitting on the grid" part if speed is 0 for a long time
    size_t firstMovingIndex = 0;
    for(size_t i = 0; i < newDriver.telemetry.size(); ++i) {
        if(newDriver.telemetry[i].speed > 5.0f) { // Found movement > 5kmh
            firstMovingIndex = i;
            break;
        }
    }
    newDriver.distanceTraveled = newDriver.telemetry[firstMovingIndex].distance;

    drivers.push_back(newDriver);
    isDataDriven = true;
    qDebug() << "Loaded" << newDriver.telemetry.size() << "telemetry entries for" << newDriver.abbreviation;
    if (!newDriver.telemetry.empty()) qDebug() << "First speed:" << newDriver.telemetry.front().speed << "Last distance:" << newDriver.telemetry.back().distance;
    
    resizeEvent(nullptr);
    update();
    return true;
}

void MonzaSimWidget::setupTrackPath() {
    // Base path initialization is now handled in loadTrack
}

void MonzaSimWidget::resizeEvent(QResizeEvent *event) {
    if (event) QWidget::resizeEvent(event);
    
    if (background.isNull()) return;

    // Calculate the best fit for the image while keeping aspect ratio
    QSize scaledSize = background.size();
    scaledSize.scale(size(), Qt::KeepAspectRatio);
    
    // Center the image in the widget
    m_drawRect = QRect(QPoint((width() - scaledSize.width()) / 2, (height() - scaledSize.height()) / 2), scaledSize);

    // Map the normalized path (0.0 - 1.0) to the specific image area
    QTransform scaler;
    scaler.translate(m_drawRect.left(), m_drawRect.top());
    scaler.scale(m_drawRect.width(), m_drawRect.height());
    scaledPath = scaler.map(trackPath);
}

void MonzaSimWidget::mousePressEvent(QMouseEvent *event) {
    // PRECISION TOOL: Click on the circuit line in the window
    // The coordinates will print to your terminal.
    if (m_drawRect.width() == 0 || m_drawRect.height() == 0) return;

    float normX = (float)(event->pos().x() - m_drawRect.left()) / m_drawRect.width();
    float normY = (float)(event->pos().y() - m_drawRect.top()) / m_drawRect.height();
    
    qDebug() << "Captured Point: {" << normX << "," << normY << "},";
}

void MonzaSimWidget::updateAnimation() {
    if (trackPath.elementCount() < 2) return;

    if (isDataDriven && !drivers.empty()) {
        for (auto& driver : drivers) {
            if (driver.telemetry.empty()) continue;

            // 1. Find the telemetry sample closest to our current distance
            // For performance in large files, you'd use binary search, but this works for now
            int index = 0;
            for (size_t i = 0; i < driver.telemetry.size(); ++i) {
                if (driver.telemetry[i].distance >= driver.distanceTraveled) {
                    index = i;
                    break;
                }
                index = i;
            }

            float kmh = driver.telemetry[index].speed; // Speed in km/h
            float mPerFrame = kmh * 0.2777f * 0.016f;
            
            driver.distanceTraveled += mPerFrame;

            // Reset if we reach the end of the race data
            if (driver.distanceTraveled > driver.telemetry.back().distance) 
                driver.distanceTraveled = 0;
        }
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

void MonzaSimWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 1. Draw Background
    painter.fillRect(rect(), Qt::black); 
    if (!background.isNull()) {
        painter.drawPixmap(m_drawRect, background);
    } else {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "Track Image Not Found\nCheck Terminal for Debug Info");
    }

    // 2. Optional: Draw Track Line (for debugging/visual alignment) - Commented out to avoid confusion
    // painter.setPen(QPen(QColor(255, 255, 255, 100), 2, Qt::DashLine));
    // painter.drawPath(scaledPath);

    // 3. Draw All Drivers
    for (const auto& driver : drivers) {
        if (driver.telemetry.empty()) continue;

        // Albert Park is ~5278m long. We use fmod to loop the dot around the trackPath.
        float lapLength = 5278.0f; 
        float visualProgress = std::fmod(driver.distanceTraveled, lapLength) / lapLength;

        QPointF carPos = scaledPath.pointAtPercent(visualProgress);
        
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