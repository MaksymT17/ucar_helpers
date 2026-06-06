#include "track_widget.h"
#include <QPainter>
#include <QImageReader>
#include <QMouseEvent>
#include <QFile>
#include <QStringList>
#include <QFileInfo>
#include <cmath>
#include <map>
#include <spdlog/spdlog.h>

// --- Projected 2026 Team & Driver Mappings ---
// NOTE: This is a speculative mapping for the 2026 season based on contracts
// confirmed as of mid-2024 and strong industry rumors. This data will likely
// need to be updated as the official driver market solidifies.
static const std::map<QString, QString> driverToTeam = {
    {"VER", "RBR"}, {"PER", "RBR"},       // Red Bull Racing (Assumption)
    {"HAM", "FER"}, {"LEC", "FER"},       // Hamilton's move to Ferrari is confirmed
    {"RUS", "MER"},                       // Mercedes
    {"NOR", "MCL"}, {"PIA", "MCL"},       // McLaren lineup is stable
    {"ALO", "AST"}, {"STR", "AST"},       // Aston Martin lineup is stable
    {"HUL", "AUD"}, {"SAI", "AUD"},       // Audi entry with Hülkenberg confirmed, Sainz is a strong rumor
    {"GAS", "ALP"}, {"OCO", "ALP"},       // Alpine (Assumption)
    {"ALB", "WIL"}, {"LIN", "WIL"},       // Williams (Assuming Linblad is a Williams driver for this example)
    {"TSU", "VCB"}, {"RIC", "VCB"}, {"LIN", "VCB"}, // Visa Cash App RB (Assumption, added Linblad)
    {"BEA", "HAA"}, {"MAG", "HAA"}        // Haas (Bearman is speculation)
};

static const std::map<QString, QColor> teamColors = {
    {"RBR", QColor("#0600EF")},       // Red Bull Racing (Dark Blue)
    {"FER", QColor("#E8002D")},       // Ferrari (Red)
    {"MER", QColor("#00D2BE")},       // Mercedes (Petronas Teal)
    {"MCL", QColor("#FF8700")},       // McLaren (Papaya Orange)
    {"AST", QColor("#006F62")},       // Aston Martin (Green)
    {"AUD", QColor("#D3D3D3")},       // Audi (Speculative Light Grey/Silver, replacing Sauber)
    {"ALP", QColor("#0090FF")},       // Alpine (Blue)
    {"VCB", QColor("#6692FF")},       // Visa Cash App RB (Blue)
    {"WIL", QColor("#64C4FF")},       // Williams (Light Blue)
    {"HAA", QColor("#FFFFFF")}        // Haas (White)
};

// --- RACE ANALYTICS DATA STRUCTURE ---
// Stores passively collected powertrain and aerodynamic characteristics 
struct DriverAnalytics {
    float maxLowSpeedAccel = 0.0f;         // Torque & Traction
    float maxHighSpeedAccelClean = 0.0f;   // Peak Power (Clean Air)
    float maxHighSpeedAccelDirty = 0.0f;   // Slipstream Power (Dirty Air)
    float maxBrakingDecel = 0.0f;          // Aero-Assisted Braking
    float terminalVelocityClean = 0.0f;    // Drag (Clean Air)
    float terminalVelocityDirty = 0.0f;    // Drag (Dirty Air)
    float absoluteMinSpeed = 999.0f;       // Mechanical Grip
    
    float prevSpeed = -1.0f;
    float prevTime = -1.0f;
};
static std::map<QString, DriverAnalytics> raceAnalyticsMap;

// Forward declaration for the function in virtual_gate_math.cpp
std::vector<VirtualGate> generateVirtualGatesFromTelemetry(
    const std::vector<TelemetryEntry>& telemetry,
    float gateInterval,
    float normalizedGateWidth
);

float calculateIntersectionFactor(QPointF p1, QPointF q1, QPointF p2, QPointF q2);

TrackSimulatorWidget::TrackSimulatorWidget(QWidget *parent) 
    : QWidget(parent), currentSpeed(0.0001f) {
    
    // Default maxSpeed for heuristic mode
    float targetLapTime = 79.0f;
    float framesPerLap = targetLapTime / 0.016f; // 16ms timer
    // We multiply by 1.25 to account for the speed loss in corners
    maxSpeed = (1.0f / framesPerLap) * 1.25f;

    animationTimer = new QTimer(this);
    connect(animationTimer, &QTimer::timeout, this, &TrackSimulatorWidget::updateAnimation);
    // The timer is now started by the 'Start race' button
    setMinimumSize(800, 600);
}

void TrackSimulatorWidget::loadTrack(const TrackConfig& config) {
    currentConfig = config;
    background.load(config.imagePath);
    trackPath = QPainterPath(); 

    resizeEvent(nullptr);
    update();
}

bool TrackSimulatorWidget::loadTelemetry(const QString& csvPath) {
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::warn("Could not open telemetry file: {}", csvPath.toStdString());
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
    
    struct RawEntry { float time, x, y, speed, distance, throttle, brake; int lap; };
    std::vector<RawEntry> rawData;

    while (!in.atEnd()) {
        QString line = in.readLine();
        QStringList fields = line.split(',');

        if (fields.size() < 9) continue;

        // CSV format: [Index,] TimeSeconds, X, Y, Speed, Distance, Throttle, Brake, LapNumber
        RawEntry entry;
        entry.time = fields[1].toFloat();
        entry.x = fields[2].toFloat();
        entry.y = fields[3].toFloat();
        entry.speed = fields[4].toFloat();
        entry.distance = fields[5].toFloat();
        entry.throttle = fields[6].toFloat();
        entry.brake = fields[7].toFloat();
        entry.lap = fields[8].toInt();

        // Filter out (0,0) outliers which are common in garage/pit telemetry
        // These distort the scaling and create the "lines across the middle"
        if (std::abs(entry.x) < 1.0f && std::abs(entry.y) < 1.0f) {
            continue;
        }

        rawData.push_back(entry);
    }

    if (rawData.empty()) return false;

    DriverSimState newDriver;
    newDriver.abbreviation = abb;
    newDriver.isPoleLap = fileName.contains("_POLE_", Qt::CaseInsensitive);

    // --- NEW COLOR LOGIC ---
    // 1. Determine team and assign primary color
    QString team = "UNKNOWN";
    if (driverToTeam.count(abb)) {
        team = driverToTeam.at(abb);
    }

    if (teamColors.count(team)) {
        newDriver.color = teamColors.at(team);
    } else {
        // Fallback for unknown teams using the old HSV-based method
        newDriver.color = QColor::fromHsv((drivers.size() * 40) % 360, 200, 255);
    }

    // 2. Assign border color to distinguish teammates (yellow for first, red for second)
    int teammatesFound = 0;
    if (team != "UNKNOWN") {
        for (const auto& d : drivers) {
            if (driverToTeam.count(d.abbreviation) && driverToTeam.at(d.abbreviation) == team) {
                teammatesFound++;
            }
        }
    }
    newDriver.borderColor = (teammatesFound == 0) ? Qt::yellow : Qt::red;
    // --- END NEW COLOR LOGIC ---
    
    for (size_t i = 0; i < rawData.size(); ++i) {
        TelemetryEntry t;
        t.time = rawData[i].time;
        t.originalPos = QPointF(rawData[i].x, rawData[i].y);
        t.speed = rawData[i].speed;
        t.distance = rawData[i].distance;
        t.throttle = rawData[i].throttle;
        t.brake = rawData[i].brake;
        t.lapNumber = rawData[i].lap;
        newDriver.telemetry.push_back(t);
    }

    // Start exactly at the beginning of the race telemetry (the Grid)
    newDriver.distanceTraveled = newDriver.telemetry.front().distance; 
    newDriver.lap1FinishDist = 0.0f; // Now 0.0 because Python handles normalization
    newDriver.lastIndex = 0;
    newDriver.currentLap = !newDriver.telemetry.empty() ? newDriver.telemetry.front().lapNumber : 1;
    newDriver.lapStartTime = 0.0f;
    newDriver.lastLapTime = 0.0f;
    newDriver.nextGateIndex = 0; // All drivers start before the first gate.

    drivers.push_back(newDriver);
    isDataDriven = true;

    // 3. RECOMPUTE GLOBAL BOUNDING BOX AND NORMALIZED POSITIONS
    // We now purely center the coordinates and scale them to [-0.5, 0.5].
    // All alignment (rotation/flip/scale) is offloaded to the TrackConfig
    // allowing you to align ANY circuit image without touching the core math.
    float gMinX = 1e10, gMaxX = -1e10, gMinY = 1e10, gMaxY = -1e10;

    for (const auto& d : drivers) {
        for (const auto& t : d.telemetry) {
            float rawX = t.originalPos.x();
            float rawY = t.originalPos.y();
            
            gMinX = std::min(gMinX, rawX);
            gMaxX = std::max(gMaxX, rawX);
            gMinY = std::min(gMinY, rawY);
            gMaxY = std::max(gMaxY, rawY);
        }
    }

    float rangeX = gMaxX - gMinX;
    float rangeY = gMaxY - gMinY;
    float maxRange = std::max(rangeX, rangeY);
    float centerX = (gMinX + gMaxX) / 2.0f;
    float centerY = (gMinY + gMaxY) / 2.0f;

    for (auto& d : drivers) {
        for (auto& t : d.telemetry) {
            float rawX = t.originalPos.x();
            float rawY = t.originalPos.y();
            
            float nx = (rawX - centerX) / maxRange;
            float ny = (rawY - centerY) / maxRange;
            
            t.normalizedPos = QPointF(nx, ny);
        }
        // Initialize previous position for VG crossing detection
        if (!d.telemetry.empty()) {
            d.previousInterpPos = d.telemetry.front().normalizedPos;
        }
    }

    // 4. GENERATE TRACK PATH FROM FIRST DRIVER TO REFLECT NEW GLOBAL NORMALIZATION
    if (!drivers.empty()) {
        trackPath = QPainterPath();
        if (drivers[0].telemetry.size() > 1) {
            trackPath.moveTo(drivers[0].telemetry[0].normalizedPos);
            for (size_t i = 1; i < drivers[0].telemetry.size(); ++i) {
                trackPath.lineTo(drivers[0].telemetry[i].normalizedPos);
            }
        }
    }

    spdlog::info("Loaded {} telemetry entries for {}", newDriver.telemetry.size(), newDriver.abbreviation.toStdString());
    if (!newDriver.telemetry.empty()) 
        spdlog::info("First speed: {} Last distance: {}", newDriver.telemetry.front().speed, newDriver.telemetry.back().distance);
    
    resizeEvent(nullptr);
    update();
    return true;
}

void TrackSimulatorWidget::clearTelemetry() {
    animationTimer->stop();
    drivers.clear();
    virtualGates.clear();
    virtualGatesPath = QPainterPath();
    simTime = 0.0f;
    maxRevealedIndex = 0;
    lapCompleted = false;
    isDataDriven = false;
    leaderboardTimer = 0.0f;
    raceAnalyticsMap.clear();
    // The old gate distances are no longer used, but clearing doesn't hurt.
    gateDistances.clear();
    emit leaderboardUpdated(QStringList(), 0);
    update();
}

void TrackSimulatorWidget::startRace() {
    spdlog::info("Race start triggered.");
    if (!animationTimer->isActive() && !drivers.empty() && !virtualGates.empty()) {
        // --- NEW LOGIC: Synchronize drivers to their starting gates ---
        // This runs only once when the race is first started (simTime is near zero).
        // It ensures that each driver's first target gate is the one immediately
        // in front of their grid position, solving the leaderboard shuffle at the start.
        if (simTime < 0.01f) {
            spdlog::info("Synchronizing driver start gates for new race...");
            raceAnalyticsMap.clear();
            for (auto& driver : drivers) {
                if (driver.telemetry.empty()) continue;

                // Reset individual driver state for a clean start
                driver.nextGateIndex = 0;
                driver.gateCrossingTimes.clear();
                driver.vgStartTime = -1.0f;
                driver.previousInterpPos = driver.telemetry.front().normalizedPos;

                QPointF startPos = driver.telemetry.front().normalizedPos;
                int initialGateIndex = 0;

                for (const auto& gate : virtualGates) {
                    QPointF vecToDriver = startPos - gate.center;
                    if (QPointF::dotProduct(vecToDriver, gate.normal) > 0) {
                        initialGateIndex = gate.id + 1;
                    } else {
                        break;
                    }
                }
                driver.nextGateIndex = initialGateIndex;
                spdlog::info("  - {} starts, next target is VG{}", driver.abbreviation.toStdString(), driver.nextGateIndex);
                
                raceAnalyticsMap[driver.abbreviation] = DriverAnalytics();
            }
        }

        spdlog::info("Starting animation timer.");
        animationTimer->start(16); // ~60 FPS
    } else if (virtualGates.empty()) {
        spdlog::warn("Cannot start race: Virtual Gates have not been generated yet.");
    }
}

void TrackSimulatorWidget::generateVirtualGates() {
    spdlog::info("Generate VGs button clicked.");

    const DriverSimState* poleDriver = nullptr;
    for (const auto& driver : drivers) {
        if (driver.isPoleLap) {
            poleDriver = &driver;
            break;
        }
    }
    
    const std::vector<TelemetryEntry>* telemetryForGeneration = nullptr;
    static std::vector<TelemetryEntry> singleLapTelemetry; // Static to keep data in scope for the pointer
    QString sourceDriverName;

    if (poleDriver) {
        spdlog::info("Using dedicated POLE lap data from {}", poleDriver->abbreviation.toStdString());
        telemetryForGeneration = &poleDriver->telemetry;
        sourceDriverName = poleDriver->abbreviation;
    } else {
        spdlog::warn("Could not find a POLE lap driver to generate Virtual Gates.");
        if (!drivers.empty()) {
            const DriverSimState* fallbackDriver = &drivers[0]; 
            spdlog::warn("Defaulting to first loaded driver and extracting a single lap: {}", fallbackDriver->abbreviation.toStdString());

            // Extract a single, clean lap (e.g., Lap 2) from the full race telemetry.
            singleLapTelemetry.clear();
            int targetLap = 2; // Lap 2 is usually cleaner than lap 1
            for(const auto& entry : fallbackDriver->telemetry) {
                if (entry.lapNumber == targetLap) singleLapTelemetry.push_back(entry);
            }
            // If Lap 2 had no data (e.g. short race), try Lap 1 as a backup.
            if (singleLapTelemetry.empty()) {
                spdlog::warn("Could not find any data for Lap 2, trying Lap 1.");
                for(const auto& entry : fallbackDriver->telemetry) {
                    if (entry.lapNumber == 1) singleLapTelemetry.push_back(entry);
                }
            }

            telemetryForGeneration = &singleLapTelemetry;
        } else {
            spdlog::warn("No drivers loaded, cannot generate gates.");
            return;
        }
    }

    if (!telemetryForGeneration || telemetryForGeneration->empty()) {
        spdlog::warn("Selected telemetry for VG generation is empty or invalid.");
        return;
    }

    virtualGates.clear();
    virtualGatesPath = QPainterPath();
    constexpr float gateInterval = 100.0f; // As per requirements
    float normalizedGateWidth = 0.02f; // Visual width for the gate lines, widened for robustness
    
    virtualGates = generateVirtualGatesFromTelemetry(*telemetryForGeneration, gateInterval, normalizedGateWidth);

    for (const auto& gate : virtualGates) {
        virtualGatesPath.moveTo(gate.p1);
        virtualGatesPath.lineTo(gate.p2);
    }
    spdlog::info("Generated {} virtual gates from {} data.", virtualGates.size(), sourceDriverName.toStdString());
    update();
}

void TrackSimulatorWidget::setupTrackPath() {
    // Base path initialization is now handled in loadTrack
}

void TrackSimulatorWidget::resizeEvent(QResizeEvent *event) {
    if (event) QWidget::resizeEvent(event);
    
    // Maintain aspect ratio by finding the largest square that fits in the widget
    int side = qMin(width(), height());
    float xOffset = (width() - side) / 2.0f;
    float yOffset = (height() - side) / 2.0f;

    QTransform scaler;
    scaler.translate(xOffset, yOffset);
    scaler.scale(side, side);
    scaledPath = scaler.map(trackPath);
}

void TrackSimulatorWidget::mousePressEvent(QMouseEvent *event) {
    // PRECISION TOOL: Click on the circuit line in the window
    // The coordinates will print to your terminal.
    // Normalize click position relative to the widget's full dimensions
    float normX = (float)event->pos().x() / width();
    float normY = (float)event->pos().y() / height();
    
    spdlog::info("Captured Point: {{{}, {}}},", normX, normY);
}

void TrackSimulatorWidget::updateAnimation() {
    // Do not run simulation if data isn't loaded.
    // If race has started, also require VGs to be generated.
    if (!isDataDriven || drivers.empty() || (animationTimer->isActive() && virtualGates.empty())) {
        if (animationTimer->isActive() && virtualGates.empty()) {
            spdlog::warn("Race started but Virtual Gates not generated. Please generate VGs.");
            animationTimer->stop();
        }
        return;
    }

    if (isDataDriven && !drivers.empty()) {
        float previousSimTime = simTime;
        simTime += 0.016f; // Advance session time by 16ms
        leaderboardTimer += 0.016f;

        // Update Leaderboard Order every 5 seconds (or immediately if just started)
        if (leaderboardTimer >= 5.0f || (leaderboardTimer > 0.01f && leaderboardTimer < 0.04f)) {
            leaderboardTimer = 0.0f;
            
            std::vector<DriverSimState*> sorted;
            for (auto& d : drivers) sorted.push_back(&d);

            // Sort drivers based on Virtual Gate progress
            std::sort(sorted.begin(), sorted.end(), [](const DriverSimState* a, const DriverSimState* b) {
                // Primary sort: number of gates crossed (more is better)
                if (a->nextGateIndex != b->nextGateIndex) {
                    return a->nextGateIndex > b->nextGateIndex;
                }
                // Secondary sort: for drivers on the same gate, who crossed it first (less time is better)
                if (a->nextGateIndex > 0) {
                    int lastGateIdx = a->nextGateIndex - 1;
                    if (a->gateCrossingTimes.count(lastGateIdx) && b->gateCrossingTimes.count(lastGateIdx)) {
                        return a->gateCrossingTimes.at(lastGateIdx) < b->gateCrossingTimes.at(lastGateIdx);
                    }
                }
                // Fallback: maintain original order if no other criteria match
                return false;
            });

            const DriverSimState* leader = sorted.empty() ? nullptr : sorted.front();
            int leaderLap = leader ? leader->currentLap : 0;
            QStringList entries;

            for (size_t i = 0; i < sorted.size(); ++i) {
                QString gapStr = "LEADER";
                if (i > 0 && leader) {
                    // Calculate gap based on the last commonly crossed virtual gate
                    int commonGateIndex = std::min(leader->nextGateIndex, sorted[i]->nextGateIndex) - 1;
                    if (commonGateIndex >= 0 &&
                        leader->gateCrossingTimes.count(commonGateIndex) &&
                        sorted[i]->gateCrossingTimes.count(commonGateIndex)) {
                        float gap = sorted[i]->gateCrossingTimes.at(commonGateIndex) - leader->gateCrossingTimes.at(commonGateIndex);
                        gapStr = QString("+%1s").arg(gap, 0, 'f', 3);
                    } else {
                        gapStr = "---";
                    }
                }
                // For debugging, show the last passed VG index instead of the lap number.
                int lastPassedGate = sorted[i]->nextGateIndex - 1;
                QString vgLabel = (lastPassedGate < 0) ? "Start" : QString("VG%1").arg(lastPassedGate);
                entries << QString("%1. %2 (%3) %4")
                    .arg(i + 1).arg(sorted[i]->abbreviation).arg(vgLabel).arg(gapStr);
            }
            emit leaderboardUpdated(entries, leaderLap);

            // Print out real-time Race Analytics
            spdlog::info("--- LIVE RACE ANALYTICS ({:.1f}s) ---", simTime);
            for (const auto& aDriver : sorted) {
                const auto& a = raceAnalyticsMap[aDriver->abbreviation];
                // Format strictly as JSON for database ingestion
                QString logStr = QString("{\"drv\": \"%1\", \"t\": %2, \"trq\": %3, \"pwr\": %4, \"drg\": %5, \"brk\": %6, \"grp\": %7}")
                    .arg(aDriver->abbreviation)
                    .arg(simTime, 0, 'f', 1)
                    .arg(a.maxLowSpeedAccel, 0, 'f', 1)
                    .arg(a.maxHighSpeedAccelClean, 0, 'f', 1)
                    .arg(a.terminalVelocityClean, 0, 'f', 1)
                    .arg(a.maxBrakingDecel, 0, 'f', 1)
                    .arg(a.absoluteMinSpeed, 0, 'f', 1);
                spdlog::info("{}", logStr.toStdString().c_str());
            }
            spdlog::info("----------------------------------------");
        }

        for (auto& driver : drivers) {
            if (driver.telemetry.empty()) continue;

            // Support for seeking/resets: if time jumped back, restart search from beginning
            if (simTime < driver.telemetry[driver.lastIndex].time) {
                driver.lastIndex = 0;
            }

            // Find the point that corresponds to current simulation time
            while (driver.lastIndex < driver.telemetry.size() - 1 &&
                   driver.telemetry[driver.lastIndex + 1].time <= simTime) {
                driver.lastIndex++;
            }
            
            // Interpolate position for smooth animation
            QPointF interpPos;
            float interpSpeed = 0.0f;
            size_t idx = driver.lastIndex;
            if (idx < driver.telemetry.size() - 1) {
                const auto& t0 = driver.telemetry[idx];
                const auto& t1 = driver.telemetry[idx + 1];
                float segmentDuration = t1.time - t0.time;
                float factor = (segmentDuration > 0.001f) ? std::clamp((simTime - t0.time) / segmentDuration, 0.0f, 1.0f) : 0.0f;
                interpPos = t0.normalizedPos + (t1.normalizedPos - t0.normalizedPos) * factor;
                interpSpeed = t0.speed + (t1.speed - t0.speed) * factor;
            } else {
                interpPos = driver.telemetry[idx].normalizedPos;
                interpSpeed = driver.telemetry[idx].speed;
            }

            // --- VIRTUAL GATE CROSSING DETECTION ---
            // A driver can cross multiple gates in one physics tick if the tick is long or speed is high.
            while (true) { // Loop until no more gates are crossed in this frame
                if (virtualGates.empty()) break;

                int currentGateInLap = driver.nextGateIndex % virtualGates.size();
                const auto& gate = virtualGates[currentGateInLap];
                
                // Use a robust segment-segment intersection test.
                float t = calculateIntersectionFactor(driver.previousInterpPos, interpPos, gate.p1, gate.p2);

                // A valid intersection occurs if t is between 0 and 1.
                if (t >= 0.0f && t <= 1.0f) {
                    // Also check that the crossing is in the forward direction.
                        QPointF movementVec = interpPos - driver.previousInterpPos;
                        if (QPointF::dotProduct(movementVec, gate.normal) > 0) {
                        // Precise crossing time using the interpolation factor 't'.
                        float crossingTime = previousSimTime + t * (simTime - previousSimTime);

                            // If this is the first gate crossing for this driver in the race, set their race start time.
                            if (driver.vgStartTime < 0.0f) {
                            driver.vgStartTime = crossingTime;
                        }

                            // Record the time for this specific gate, relative to their personal race start.
                        if (driver.vgStartTime >= 0.0f) {
                            driver.gateCrossingTimes[driver.nextGateIndex] = crossingTime - driver.vgStartTime;
                        }
                        
                        driver.nextGateIndex++; // Increment total gates passed
                        continue; // Check if we crossed another gate in the same frame
                    }
                }
                // If no intersection or wrong direction, stop checking for this driver this frame.
                break;
            }
            driver.previousInterpPos = interpPos;

            // Detect Lap Change
            int newLap = driver.telemetry[driver.lastIndex].lapNumber;
            if (newLap > driver.currentLap) {
                driver.lastLapTime = driver.telemetry[driver.lastIndex].time - driver.lapStartTime;
                driver.lapStartTime = driver.telemetry[driver.lastIndex].time;
                driver.currentLap = newLap;
            }

            // Track the furthest point reached across all drivers
            if (driver.lastIndex > maxRevealedIndex) {
                maxRevealedIndex = driver.lastIndex;
            }

            // Keep distanceTraveled synced for any distance-based logic
            driver.distanceTraveled = driver.telemetry[driver.lastIndex].distance;

            // Keep track line visible once the lead driver finishes
            if (&driver == &drivers[0] && driver.lastIndex >= (int)driver.telemetry.size() - 1 && !lapCompleted) {
                spdlog::info("updateAnimation lapCompleted");
                lapCompleted = true;
            }

            // --- PASSIVE RACE ANALYTICS COLLECTION ---
            // This block strictly observes data and does not modify simulation flow.
            if (idx < driver.telemetry.size()) {
                const auto& tData = driver.telemetry[idx];
                auto& analytics = raceAnalyticsMap[driver.abbreviation];
                
                // Determine Dirty Air: Is there another car less than 150m ahead of us?
                bool inDirtyAir = false;
                for (const auto& other : drivers) {
                    if (&other == &driver) continue;
                    float distDiff = other.distanceTraveled - driver.distanceTraveled;
                    if (distDiff > 0.0f && distDiff < 150.0f) {
                        inDirtyAir = true;
                        break;
                    }
                }

                if (analytics.prevTime > 0.0f && simTime > analytics.prevTime) {
                    float dt = simTime - analytics.prevTime;
                    float dv = interpSpeed - analytics.prevSpeed; // Use smooth interpolated speed
                    float accel = dv / dt; // Acceleration in (km/h)/s

                    // Filter out unrealistic telemetry physics spikes (illusions) before saving them to analytics
                    if (accel > -150.0f && accel < 100.0f) {
                        // Powertrain: Torque / Traction (Low Speed)
                        if (interpSpeed < 150.0f && tData.throttle > 98.0f) analytics.maxLowSpeedAccel = std::max(analytics.maxLowSpeedAccel, accel);
                        
                        // Powertrain: Peak Power vs Drag (High Speed)
                        if (interpSpeed > 250.0f && tData.throttle > 98.0f) {
                            if (inDirtyAir) analytics.maxHighSpeedAccelDirty = std::max(analytics.maxHighSpeedAccelDirty, accel);
                            else analytics.maxHighSpeedAccelClean = std::max(analytics.maxHighSpeedAccelClean, accel);
                        }

                        // Aerodynamics: Aero-Assisted Braking
                        if (interpSpeed > 250.0f && tData.brake > 0.0f) analytics.maxBrakingDecel = std::min(analytics.maxBrakingDecel, accel);
                    }

                    // Aerodynamics: Drag (Terminal Velocity)
                    if (inDirtyAir) analytics.terminalVelocityDirty = std::max(analytics.terminalVelocityDirty, interpSpeed);
                    else analytics.terminalVelocityClean = std::max(analytics.terminalVelocityClean, interpSpeed);

                    // Aerodynamics/Chassis: Mechanical Grip (Only sample after Turn 1 chaos > 500m)
                    if (driver.distanceTraveled > 500.0f && interpSpeed < analytics.absoluteMinSpeed && interpSpeed > 20.0f) analytics.absoluteMinSpeed = interpSpeed;
                }
                
                analytics.prevSpeed = interpSpeed;
                analytics.prevTime = simTime;
            }
        }
        update();
        return;
    }

    // Fallback heuristic for a single ghost car is now effectively disabled by the guards above.
}

void TrackSimulatorWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Helper for timing format: M:SS.ms
    auto formatTime = [](float seconds) -> QString {
        if (seconds <= 0) return "--:--.---";
        int m = static_cast<int>(seconds) / 60;
        float s = std::fmod(seconds, 60.0f);
        return QString("%1:%2").arg(m).arg(s, 6, 'f', 3, '0');
    };

    // 1. Draw Background
    painter.fillRect(rect(), Qt::black);

    // Calculate square viewport to maintain aspect ratio
    int side = qMin(width(), height());
    float xOffset = (width() - side) / 2.0f;
    float yOffset = (height() - side) / 2.0f;

    if (!background.isNull()) {
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        
        // Translate to the center of the viewport
        painter.translate(xOffset + side / 2.0f, yOffset + side / 2.0f);
        
        // Draw image perfectly centered, filling the track bounds area.
        float imgScale = (float)side / qMax(background.width(), background.height());
        painter.scale(imgScale, imgScale);
        
        // Draw image centered
        painter.drawPixmap(-background.width() / 2, -background.height() / 2, background);
        painter.restore();
    }

    // Apply the custom configuration transformations to the telemetry data
    QTransform trackTransform;
    trackTransform.translate(xOffset + side / 2.0f, yOffset + side / 2.0f); // Move to center
    trackTransform.translate(currentConfig.offsetX * side, currentConfig.offsetY * side);
    trackTransform.scale(side * currentConfig.scale, side * currentConfig.scale);
    trackTransform.rotate(currentConfig.rotation);
    trackTransform.scale(currentConfig.flipX ? -1.0 : 1.0, currentConfig.flipY ? -1.0 : 1.0);

    auto mapPos = [&](QPointF norm) {
        return trackTransform.map(norm);
    };

    // 2. Draw Track Line (Reveal logic)
    if (!drivers.empty()) {
        const auto& refTelemetry = drivers[0].telemetry;
        // Clamp limit to reference telemetry size to prevent crashes
        size_t limit = lapCompleted ? refTelemetry.size() : std::min(maxRevealedIndex, refTelemetry.size());

        if (limit > 1) {
            painter.setPen(QPen(QColor(255, 255, 255, 80), 2));
            QPainterPath revealPath;
            revealPath.moveTo(mapPos(refTelemetry[0].normalizedPos));

            for (size_t i = 1; i < limit; ++i) {
                revealPath.lineTo(mapPos(refTelemetry[i].normalizedPos));
            }
            painter.drawPath(revealPath);
        }
    }

    // 2.5. Draw Virtual Gates if they have been generated
    if (!virtualGatesPath.isEmpty()) {
        painter.setPen(QPen(QColor(150, 120, 180), 1));
        painter.drawPath(trackTransform.map(virtualGatesPath));
    }

    // 3. Draw All Drivers
    for (const auto& driver : drivers) {
        if (driver.telemetry.empty()) continue;
        
        // SKIP POSITIONING: If driver crashed or finished, their telemetry stops.
        // If simTime is past their final data point, don't draw them.
        if (simTime < driver.telemetry.front().time || simTime > driver.telemetry.back().time) {
            continue;
        }

        // The driver's position for the current frame was calculated in updateAnimation
        // and is stored in previousInterpPos. We draw the car at this position.
        // This ensures drawing is consistent with the logic (like VG crossings).
        QPointF carPos = mapPos(driver.previousInterpPos);
        
        painter.setBrush(driver.color);
        painter.setPen(QPen(driver.borderColor, 2)); // Use team-specific border color
        painter.drawEllipse(carPos, 9, 9); // Slightly smaller radius to account for thicker border
        // Set label color to light grey for better readability
        painter.setPen(QPen(Qt::white, 1));
        // Draw Simple Label: Just the abbreviation to reduce redundancy
        QString label = driver.abbreviation;
        painter.drawText(carPos + QPointF(12, 5), label);
    }

    if (drivers.empty()) {
        painter.setPen(Qt::yellow);
        painter.drawText(20, 30, "Waiting for telemetry data...");
    }
}