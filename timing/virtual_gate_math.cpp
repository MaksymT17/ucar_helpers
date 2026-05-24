#include "track_widget.h" // For full definition of TelemetryEntry
#include <cmath>
#include <QDebug>

// Helper to normalize a QPointF vector to get a unit vector
static QPointF getUnitVector(QPointF v) {
    float length = std::sqrt(v.x() * v.x() + v.y() * v.y());
    if (length > 1e-5f) { // Use a small epsilon to avoid division by zero
        return v / length;
    }
    return QPointF(0, 0); // Return zero vector if input is zero
}

/**
 * @brief Generates Virtual Gates along a telemetry path at fixed distance intervals.
 * 
 * This function implements the logic described in the requirements:
 * 1. It iterates through the telemetry, which is a series of (X,Y) points with distance data.
 * 2. For each 200m interval, it finds the telemetry segment (between a "previous" and "current" point)
 *    where the 200m mark is crossed.
 * 3. It performs a "clinically precise" interpolation to find the exact (X,Y) coordinate on that
 *    segment corresponding to the 200m mark.
 * 4. It calculates a line perpendicular to the car's direction of travel at that precise point.
 * 5. This perpendicular line is stored as a Virtual Gate.
 * 6. The process repeats for the next 200m interval until the end of the lap data.
 */
std::vector<VirtualGate> generateVirtualGatesFromTelemetry(
    const std::vector<TelemetryEntry>& telemetry,
    float gateInterval,      // The distance between each gate (e.g., 200.0m)
    float normalizedGateWidth // The visual width of the gate in normalized coordinates
) {
    std::vector<VirtualGate> gates;
    if (telemetry.size() < 2) {
        qWarning() << "Not enough telemetry points to generate gates.";
        return gates;
    }

    // Per requirements, we start from the beginning of the provided lap data.
    // The first distance marker is our starting point.
    const float startDist = telemetry.front().distance;
    
    // The distance at which the *next* virtual gate should be placed.
    // We start with the first 200m mark from the beginning of the lap.
    float nextGateDist = startDist + gateInterval;
    
    int gateId = 0;

    // Iterate over every segment of the lap (from point i to i+1)
    for (size_t i = 0; i < telemetry.size() - 1; ++i) {
        const auto& prevPoint = telemetry[i];
        const auto& currPoint = telemetry[i+1];

        // A single telemetry segment can sometimes be long enough to contain multiple gates,
        // especially if telemetry sampling rate is low or speed is very high.
        // This 'while' loop ensures we generate all gates that fall within this single segment.
        while (prevPoint.distance <= nextGateDist && currPoint.distance >= nextGateDist) {
            
            const QPointF prevPos = prevPoint.normalizedPos;
            const QPointF currPos = currPoint.normalizedPos;

            const float segmentTotalDist = currPoint.distance - prevPoint.distance;

            if (segmentTotalDist <= 1e-6f) {
                break; // Cannot interpolate, move to the next segment.
            }

            const float distanceIntoSegment = nextGateDist - prevPoint.distance;
            const float interpolationFactor = distanceIntoSegment / segmentTotalDist;

            const QPointF gateCenterPos = prevPos + (currPos - prevPos) * interpolationFactor;
            const QPointF tangent = getUnitVector(currPos - prevPos);
            const QPointF normal = QPointF(-tangent.y(), tangent.x());

            const QPointF p1 = gateCenterPos - normal * (normalizedGateWidth / 2.0f);
            const QPointF p2 = gateCenterPos + normal * (normalizedGateWidth / 2.0f);

            gates.push_back({gateId++, nextGateDist, gateCenterPos, p1, p2, normal});
            
            nextGateDist += gateInterval;
        }
    }
    return gates;
}