# Virtual Gates Timing System Implementation

This document outlines the implementation of the Virtual Gates (VG) timing system, designed to provide a "clinically precise" and absolute metric for measuring lap progress and calculating race standings. The system is broken down into three core stages: Generation, Measurement, and Application.

---

## 1. Generation: Defining the Checkpoints

The foundation of the timing system is a set of precisely placed "mini finish lines" around the track.

### How It Works

1.  **Reference Lap**: Before the race simulation starts, the system analyzes a clean, complete lap from a single reference driver. The dedicated pole position lap is used if available; otherwise, a clean lap (e.g., Lap 2) is extracted from the first available driver.

2.  **Distance-Based Placement**: The system iterates through the reference lap's telemetry data. For every **200-meter** interval, it identifies the exact point on the track that corresponds to that distance.

3.  **Precise Interpolation**: Since telemetry points are discrete, the system performs a linear interpolation between the two data points that straddle the 200m mark. This is based on the `distance` property of the telemetry, ensuring the gate's position is not just close to the 200m mark, but exactly on it.

    ```cpp
    // From virtual_gate_math.cpp
    const float distanceIntoSegment = nextGateDist - prevPoint.distance;
    const float interpolationFactor = distanceIntoSegment / segmentTotalDist;

    const QPointF gateCenterPos = prevPos + (currPos - prevPos) * interpolationFactor;
    ```

4.  **Perpendicular Gate Line**: At the interpolated center point, the system calculates the car's direction of travel (the tangent). A line perpendicular to this tangent is then created. This line is the Virtual Gate.

    ```cpp
    // From virtual_gate_math.cpp
    const QPointF tangent = getUnitVector(currPos - prevPos);
    const QPointF gateLineDir = QPointF(-tangent.y(), tangent.x()); // Perpendicular vector

    const QPointF p1 = gateCenterPos - gateLineDir * (normalizedGateWidth / 2.0f);
    const QPointF p2 = gateCenterPos + gateLineDir * (normalizedGateWidth / 2.0f);
    ```

### Result

The output of this stage is a `std::vector<VirtualGate>`, which is a collection of line segments distributed around the track. This process is handled by the `generateVirtualGatesFromTelemetry` function.

---

## 2. Measurement: Calculating Precise Crossing Time

During the race, the system must detect the exact moment each driver crosses each gate.

### How It Works

1.  **Segment Intersection**: In each frame of the simulation, the driver's movement is represented as a line segment from their position in the previous frame (`previousInterpPos`) to their current position (`interpPos`). The system checks if this movement segment intersects with the next target Virtual Gate line segment.

2.  **Interpolation for Time**: The `calculateIntersectionFactor` function determines *where* along the driver's movement segment the intersection occurred. It returns a factor `t` between 0.0 and 1.0.

    -   `t = 0.0` means the intersection happened exactly at the start of the movement.
    -   `t = 1.0` means it happened at the end.
    -   `t = 0.5` means it happened exactly halfway through the frame's movement.

3.  **Precise Timestamp**: This factor `t` is used to interpolate between the previous frame's simulation time and the current time, yielding a "clinically precise" timestamp for the crossing event.

    ```cpp
    // From track_widget.cpp
    float t = calculateIntersectionFactor(driver.previousInterpPos, interpPos, gate.p1, gate.p2);

    if (t >= 0.0f && t <= 1.0f) {
        // Precise crossing time using the interpolation factor 't'.
        float crossingTime = previousSimTime + t * (simTime - previousSimTime);
        driver.gateCrossingTimes[driver.nextGateIndex] = crossingTime - driver.vgStartTime;
    }
    ```

### Result

For every driver, the system populates a map (`gateCrossingTimes`) that stores a highly accurate, interpolated timestamp for each Virtual Gate they pass.

---

## 3. Application: Building the Leaderboard

With precise crossing times for every driver at hundreds of points around the track, building a synchronized leaderboard becomes straightforward and robust.

1.  **Sorting**: Drivers are sorted primarily by the number of gates they have crossed (`nextGateIndex`). If two drivers have crossed the same number of gates, they are sorted secondarily by who crossed the last common gate first (i.e., the one with the lower timestamp).

2.  **Calculating Gaps**: The time gap between any two drivers is the difference between their recorded crossing times at the last gate that *both* have passed. This provides an "absolute metric" that resolves the discrepancies and synchronization problems seen in simpler timing systems, especially during the staggered start of the first lap.