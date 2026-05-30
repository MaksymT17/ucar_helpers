# EV Powertrain Simulation Implementation Details

This document summarizes the technical architecture and logic for the EV Powertrain simulation project, covering both the Python prototype and the C++/Qt production core.

## 1. Project Architecture
The project evolved from a Python/Tkinter prototype to a high-performance C++/Qt6 application.

- **Python Version**: Research prototype using `threading` for a 20Hz physics loop.
- **C++ Version**: Production core using `EVPowertrainSimulator`. It models the full **Powertrain** (Battery -> Inverter -> Motor) and the resulting **Drivetrain** forces.

## 2. Physics Engine (`EVPowertrainSimulator`)
The simulation calculates real-time dynamics based on a Tesla Model 3 RWD reference.

### Propulsion & Energy Flow
- **Energy Path**: Battery DC -> Inverter AC -> Motor Torque -> Drivetrain Gear Reduction -> Wheel Force.
- **Power Budgeting**: Total battery discharge is shared between propulsion and auxiliary systems (AC, Lights, Infotainment).
- **Drive Torque**: Constant torque region (up to 5000 RPM) at `3600 Nm` (wheel), followed by a constant power region limited by net battery power (nominal `210 kW`).
- **Efficiency Models**: 
  - **Motor**: Dynamic efficiency based on Copper losses (load-dependent) and Iron losses (RPM-dependent). Base η = 0.95.
  - **Inverter**: Switching loss model based on throttle position. Base η = 0.98.
- **Power Limiting**: 
  - `1.0` (Normal)
  - `0.7` (Thermal Derate)
  - `0.0` (Emergency Shutdown)
- **Structural RPM Limiter**: Hard safety ceiling at `18,000 RPM` (Tesla RDU limit), preventing the vehicle from exceeding ~247 km/h with a 9.04:1 gear ratio.
- **Braking**: 0.0 to 0.5 pedal range handles regenerative braking (`1800 Nm` peak); 0.5 to 1.0 adds mechanical disk brakes (`8.0 m/s²` deceleration).

### Resistance Forces
- **Dynamic Air Density**: Calculated via the Ideal Gas Law (`P / (R*T)`) based on ambient temperature. Cold air increases drag and energy consumption realistically.
- **Aerodynamic Drag**: `Fd = 0.5 * (Cd * multiplier) * Area * AirDensity * v_rel²`. Base `Cd = 0.25`.
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
- **Escalation**: TURNED_OFF -> ACTIVE_FAN -> LIQUID_COLD.
- **Thermal Derate**: Triggered if any component exceeds `normal_max` (Motor 90°C, Inv 75°C, Bat 45°C).
- **Emergency Shutdown**: Triggered if components hit critical limits (Motor 140°C, Inv 120°C, Bat 50°C).

## 4. Qt UI Implementation (`SpecViewer`)

### Layout
- **Three-Column Grid**: Optimised layout with column weights set to **8 (Controls/Stats) : 9 (Thermal) : 10 (System Info)**.
- **Global Style**: Custom dark theme using Qt's `Fusion` style with a black/cyan/green palette.
- **Stats Grid**: A consolidated 2-column grid in the first column combining real-time telemetry (Battery SOC, Distance, Time) and hardware cooling states.
- **Infotainment & Climate**: Dedicated panel in column 1 for managing auxiliary loads:
  - **Climate**: Dynamic AC power draw based on ambient vs. target temperature delta.
  - **Lighting**: Logical toggles for Low Beam (100W) and High Beam (250W).

### Custom Components
- **PowerGraph**: A custom `QWidget` with a fixed ±210kW scale. Includes logic to ensure small auxiliary loads (e.g., 150W infotainment) are rendered with at least a 1-pixel height for visibility.
- **Gradient Swiper**: A `QSlider` styled via QSS to look like a digital vertical track.
- **Car Schematic**: A `QLabel` containing a `QPixmap` that is rotated in real-time using `QTransform` based on the surface gradient angle.

### Fixes and Optimizations
- **Rotation**: Positive gradient tilts the car nose-up (counter-clockwise).
- **Compact Telemetry**: Reduced padding and grouped labels into a `Stats Grid` for a high-density "cockpit" look.
- **SOC Display**: Battery state includes both energy remaining (kWh) and percentage (SOC%).
- **Anti-Jitter**: `gradientValueLabel` uses `QString::asprintf("%+5.1f", pct)` and fixed-width descriptive strings (e.g., "uphill  ") to prevent layout shifting.
- **Performance**: C++ version shows >6x reduction in CPU usage compared to Python due to native compilation and optimized `QPainter` calls.

## 5. Master Parameters (Executive Spec)
| Key | Value |
| :--- | :--- |
| MASS | 1850 kg |
| WHEEL_RADIUS | 0.33 m |
| BATTERY_CAPACITY| 75 kWh |
| PEAK_POWER | 210 kW (nominal) |
| DT | 0.05 s |

## 6. Directory Structure
```text
/powertrain
├── implementation_details.md
├── thermal_system_design_notes.md
├── powertrain_scheme2.png
├── app_icon.jpg
├── top_view.png
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