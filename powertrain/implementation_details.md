# EV Powertrain Simulation Implementation Details

This document summarizes the technical architecture and logic for the EV Powertrain simulation project, covering both the Python prototype and the C++/Qt production core.

## 1. Project Architecture
The project evolved from a Python/Tkinter prototype to a high-performance C++/Qt6 application.

- **Python Version**: Uses `threading` and `Lock` for a dedicated physics loop at 50ms (20Hz). GUI uses `tkinter` and `PIL` for car image rotations.
- **C++ Version**: Uses a `QTimer` driven main loop (20Hz) in the GUI thread. The logic is encapsulated in `EVPowertrainSimulator`, and the UI in `SpecViewer`.

## 2. Physics Engine (`EVPowertrainSimulator`)
The simulation calculates real-time dynamics based on a Tesla Model 3 RWD reference.

### Torque & Power
- **Drive Torque**: Constant torque region (up to 5000 RPM) at `3600 Nm` (wheel), followed by a constant power region limited at `210 kW`.
- **Power Limiting**: 
  - `1.0` (Normal)
  - `0.7` (Thermal Derate)
  - `0.0` (Emergency Shutdown)
- **Braking**: 0.0 to 0.5 pedal range handles regenerative braking (`1800 Nm` peak); 0.5 to 1.0 adds mechanical disk brakes (`8.0 m/s²` deceleration).

### Resistance Forces
- **Aerodynamic Drag**: `Fd = 0.5 * (Cd * multiplier) * Area * Density * v²`. Base `Cd = 0.23`.
- **Rolling Resistance**: `Fr = Crr * mass * g`. `Crr` varies by surface (Asphalt: 0.012, Gravel: 0.025, Ice: 0.005).
- **Gradient Force**: `Fg = mass * g * sin(theta)`.

## 3. Thermal Management
A three-mass thermal system with two independent cooling loops.

### Thermal Masses
- **Motor**: 22,000 J/°C
- **Battery**: 309,000 J/°C
- **Inverter**: 3,400 J/°C

### Cooling Logic (`CoolingAction` enum)
- Polled every **5 seconds** to maintain stability.
- **Escalation**: NONE -> PASSIVE -> ACTIVE_FAN -> LIQUID_COLD.
- **Thermal Derate**: Triggered if any component exceeds `normal_max` (Motor 90°C, Inv 75°C, Bat 45°C).
- **Emergency Shutdown**: Triggered if components hit critical limits (Motor 140°C, Inv 120°C, Bat 50°C).

## 4. Qt UI Implementation (`SpecViewer`)

### Layout
- **Three-Column Grid**: Column weights are set to **8 (Controls) : 9 (Thermal) : 10 (System Info)**.
- **Global Style**: Custom dark theme using Qt's `Fusion` style with a black/cyan/green palette.

### Custom Components
- **PowerGraph**: A custom `QWidget` using `QPainter`. Manages a 300-point rolling history. Draws orange/red bars for consumption and green for regeneration.
- **Gradient Swiper**: A `QSlider` styled via QSS to look like a digital vertical track.
- **Car Schematic**: A `QLabel` containing a `QPixmap` that is rotated in real-time using `QTransform` based on the surface gradient angle.

### Fixes and Optimizations
- **Rotation**: Positive gradient tilts the car nose-up (counter-clockwise).
- **Anti-Jitter**: `gradientValueLabel` uses `QString::asprintf("%+5.1f", pct)` and fixed-width descriptive strings (e.g., "uphill  ") to prevent layout shifting.
- **Performance**: C++ version shows >6x reduction in CPU usage compared to Python due to native compilation and optimized `QPainter` calls.

## 5. Master Parameters (Executive Spec)
| Key | Value |
| :--- | :--- |
| MASS | 1850 kg |
| WHEEL_RADIUS | 0.33 m |
| BATTERY_CAPACITY| 75 kWh |
| PEAK_POWER | 210 kW |
| DT | 0.05 s |

## 6. Directory Structure
```text
/powertrain
├── implementation_details.md   <-- You are here
├── thermal_system_design_notes.md
├── powertrain_scheme2.png
├── python/
│   └── ev_simulation.py
└── qt/
    ├── CMakeLists.txt
    ├── main.cpp
    ├── powertrain_simulator.h/cpp
    ├── specviewer.h/cpp
    └── powergraph.h/cpp
```

## 7. Future Roadmap Items
- [ ] Implementation of Slew-Rate Limiter for power transitions in C++.
- [ ] Battery Cold-Soak / Pre-heat logic (Liquid Warm mode).
- [ ] High-precision `steady_clock` for C++ physics loop timing.
- [ ] Altitude effects on air density and cooling.

---
*Document Version: 1.0*
*Last Updated: May 2026*