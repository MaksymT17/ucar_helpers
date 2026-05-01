# Thermal System Design Notes
## EV Simulation — Session Log

---

## Vehicle Base Parameters

| Parameter | Value | Notes |
|---|---|---|
| Mass | 1850 kg | Target kerb mass (aligned with day2 spec) |
| Max wheel torque | 3600 Nm | Tesla Model 3 RWD reference |
| Peak power | 210 kW | Motor-limited per spec (software derate of Tesla RDU hardware) |
| Battery capacity | 75 kWh | |
| Powertrain efficiency | 0.88 | Inverter + motor combined |
| Wheel radius | 0.33 m | |
| Simulation timestep | 50 ms | Physics stability |

---

## Thermal Devices

Three independently modelled thermal masses:

| Device | Thermal Mass (J/°C) | Composition basis |
|---|---|---|
| Motor | 22,000 | Windings + Stator + Housing (Tesla RDU ref) |
| Battery | 309,000 | Full 4416-cell pack (~480kg assembly) |
| Inverter | 3,400 | SiC Cold plate + Logic mass |

### Temperature Bands (°C)

| Device | Optimal min | Optimal max | Normal max | Emergency max |
|---|---|---|---|---|
| Motor | 40 | 80 | 90 | 140 |
| Battery | 20 | 35 | 45 | 50 |
| Inverter | 35 | 65 | 75 | 120 |

- **Optimal** — sweet spot, no active cooling needed
- **Normal max** — Triggers **Intermediate Mode** (70% Power Derate)
- **Emergency max** — Triggers hard **Emergency Shutdown**

---

## Heat Generation Physics

### Motor
```
η_motor = η0 - k_copper · throttle² - k_iron · (rpm / rpm_max)
Q_motor = P_mech · (1 - η_motor)
```
- η0 = 0.95, k_copper = 0.08, k_iron = 0.04, floor η = 0.70
- Copper losses ∝ I² ∝ throttle² (load-dependent)
- Iron/eddy losses ∝ RPM (speed-dependent)

### Inverter
```
η_inv = η0 - k_switch · throttle
Q_inv = P_mech · (1 - η_inv)
```
- η0 = 0.98, k_switch = 0.03, floor η = 0.88
- IGBT switching losses scale with current

### Battery
```
I = P_electrical / V_nominal
Q_bat = I² · R_internal
```
- R_internal = 0.02 Ω (~200 series cells × 0.1 mΩ/cell)
- V_nominal = 400 V
- Pure Joule heating — no flat percentage, physics-based
- Note: original 0.05 Ω was corrected — gave unrealistic 17 kW at highway speed

---

## Cooling System

### Newton's Law of Cooling
```
dT/dt = (Q_generated - Q_dissipated) / C_thermal
Q_dissipated = h · (T_component - T_reference)
```
- For air cooling: T_reference = ambient
- For liquid cooling: T_reference = coolant temperature
- Natural convection (h = 5 W/°C) always active as baseline — nothing is perfectly insulated

### Cooling Mode h Values (W/°C)

| Mode | h (W/°C) | Notes |
|---|---|---|
| Natural convection | 5 | Always on, baseline |
| Passive (fins open) | 15 | Added on top of natural |
| Active fan | 60 | Forced air (high load / stationary) |
| Liquid cold | 200 | Component → coolant heat transfer |
| Liquid warm | 80 | Passive liquid (warm-up mode) |

### 4-Zone Cooling State Machine
Safety checks for Emergency and Derate are performed every **50ms** for immediate protection. 
Cooling hardware (pumps/fans) escalates one step every **5s** to prevent mechanical cycling:
```
T ≤ optimal_max              → step down toward NONE
optimal_max < T ≤ normal_max → PASSIVE (ram air helps at speed)
T > normal_max               → escalate: PASSIVE → FAN → LIQUID_COLD (one step per tick)
T > emergency_max            → emergency shutdown flag
```
State escalates one step per 50 ms tick — no instant jumps to liquid cooling.

---

## Two Independent Coolant Loops

Key design decision: battery and motor/inverter run separate loops because they have different target temperatures. Sharing one loop would push hot motor coolant into the battery.

| Loop | Components | Thermal Mass (J/°C) | Radiator h (W/°C) | Notes |
|---|---|---|---|---|
| Powertrain (PT) | Motor + Inverter | 23,000 | 180 | ~6L glycol, 6.6 kg |
| Battery (Bat) | Battery only | 15,000 | 250 | ~4L glycol, chiller-assisted |

### Coolant Heat Exchange
When a component is in LIQUID_COLD or LIQUID_WARM mode:
```
Q_to_coolant = h_liquid · (T_component - T_coolant)
```
Cooling effectiveness degrades naturally as coolant heats up — ΔT shrinks.
Each loop tracks `heat_to_coolant` from its components and updates independently.

### Ram Air Effect on Radiator
```
h_eff = h_base + RAM_AIR_K · v²
```
- RAM_AIR_K = 0.8 W/°C·(m/s)²
- At 100 km/h (27.8 m/s): +618 W/°C free cooling on top of base radiator h
- Means at highway speed, passive mode is often sufficient — fan not needed
- Both coolant loops benefit from ram air

---

## Bug Fixed: Zero-Throttle Freeze

**Problem:** With `cooling_action = NONE`, h = 0, so Newton's law gave zero dissipation even at idle. Components froze at elevated temperature after throttle cut.

**Fix:** Natural convection (h = 5 W/°C) added as always-on baseline:
```python
h = NATURAL_CONVECTION + {active_cooling_map}.get(action, 0)
```
Components now always bleed heat to ambient proportional to ΔT, even with all systems off.

---

## GUI Layout

Two-column layout to fit screen height:
- **Left column:** throttle slider, EV parameters, speed, battery, distance, emergency status
- **Right column:** ambient temp slider, temperature readings (with colour coding), coolant temps, cooling actions

Temperature colour coding:
- Black → within normal range
- Orange → above normal_max (cooling escalating)
- Red → above emergency_max

---

## Potential Future Work

- **Regenerative braking heat** — regen current reversal still causes I²R heating in battery
- **Cold soak / pre-heat** — battery below optimal_min should trigger LIQUID_WARM before allowing full power (Tesla behaviour)
- **Motor derating curve** — reduce max torque proportionally as motor approaches normal_max, before hard emergency cutoff
- **Altitude effect** — thinner air reduces both ram air cooling effectiveness and aerodynamic drag
