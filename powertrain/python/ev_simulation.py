import tkinter as tk
from tkinter import ttk
import os

# --- EV PARAMETERS (Executive Master Spec) ---
MASS = 1850                # kg (Target Kerb Mass)
MAX_WHEEL_TORQUE = 3600    # Nm (Tesla Model 3 RWD Wheel Torque)
MAX_POWER = 220000         # W (220 kW Peak)
BATTERY_CAPACITY = 75 * 3600000  # 75 kWh to Joules
EFFICIENCY = 0.88          # Powertrain efficiency (Inverter + Motor)
WHEEL_RADIUS = 0.33        # m
DT = 0.05                  # 50ms time step for physics stability

# --- RESISTANCE FACTORS ---
FRONTAL_AREA = 2.2         # m^2 (Optimized Notchback Sedan)
AIR_DENSITY = 1.225        # kg/m^3
DRAG_COEFF = 0.23          # Aero target
GRAVITY = 9.81             
ROLLING_RESIST_COEFF = 0.012 # Typical EV Low-Rolling Resistance tires

# --- THERMAL SYSTEM PARAMETERS ---
# Temperature limits (°C)
# Emergency thresholds set at -20% of Tesla Model 3 real hard limits, rounded:
#   Motor:    bearing limit 130°C  × 0.8 → 105°C
#   Inverter: SiC MOSFET limit 150°C × 0.8 → 120°C
#   Battery:  cell limit 60°C      × 0.8 →  50°C
MOTOR_TEMP_NORMAL    = (0,   80)
MOTOR_TEMP_OPTIMAL   = (40,  60)
MOTOR_TEMP_EMERGENCY = (-20, 105)
BATTERY_TEMP_NORMAL  = (15,  40)
BATTERY_TEMP_OPTIMAL = (20,  35)
BATTERY_TEMP_EMERGENCY = (-10, 50)
INVERTER_TEMP_NORMAL   = (0,   85)
INVERTER_TEMP_OPTIMAL  = (35,  65)
INVERTER_TEMP_EMERGENCY = (-20, 120)

# Thermal constants
AMBIENT_TEMP = 25.0        # °C (default outside temperature)
# Thermal capacitance C = m * c_p  [J/°C]  — Tesla Model 3 reference
# Motor: copper windings 8kg(c_p=385) + steel lam 20kg(c_p=490) + Al housing 10kg(c_p=900) → 21,880
# Battery: 4416x 21700 cells × 70g × c_p=1000 J/kg·°C → 309,000 J/°C
# Inverter: SiC cold plate Al 3kg(c_p=900) + components 1kg(c_p=700) → 3,400
MOTOR_THERMAL_MASS    = 22000.0   # J/°C
BATTERY_THERMAL_MASS  = 309000.0  # J/°C  — full 4416-cell pack
INVERTER_THERMAL_MASS = 3400.0    # J/°C

# Heat dissipation: Newton cooling coefficient h [W/°C]
# Q_cool = h * (T_component - T_ambient)
NATURAL_CONVECTION = 5      # W/°C  always-on baseline (no system needed)
PASSIVE_COOLING = 15        # W/°C  passive fins/vents
ACTIVE_FAN_COOLING = 60     # W/°C  forced air
LIQUID_COOLING_ACTIVE = 200 # W/°C  heat transfer: component → coolant
LIQUID_COOLING_PASSIVE = 80 # W/°C  liquid cooling (passive/warm)

# Ram air: h_eff = h_base + RAM_AIR_K * v²
# Radiator frontal area ~0.06m², forced convection h ∝ v^0.8 empirically
# At 100 km/h (27.8 m/s): boost ≈ 0.06 * 27.8² ≈ 46 W/°C — total ~80-120 W/°C realistic
RAM_AIR_K = 0.06  # W/°C·(m/s)²  — corrected from 0.8 (was 10x too high)
# Powertrain loop (motor + inverter): ~9L 50/50 glycol, m=9.6kg, c_p=3500 → 33,700 J/°C
COOLANT_PT_THERMAL_MASS = 33700.0   # J/°C
COOLANT_PT_RADIATOR_H   = 80.0      # W/°C  base radiator (ram air adds on top)
# Battery loop: ~6L glycol, m=6.4kg, c_p=3500 → 22,400 J/°C, chiller-assisted
COOLANT_BAT_THERMAL_MASS = 22400.0  # J/°C
COOLANT_BAT_RADIATOR_H   = 120.0    # W/°C  chiller-assisted, higher base than PT

# Regenerative braking
REGEN_MAX_TORQUE   = 1800.0  # Nm at wheels — ~50% of drive torque, typical EV regen limit
REGEN_EFFICIENCY   = 0.85    # round-trip: kinetic -> electrical -> battery
MECH_BRAKE_DECEL   = 8.0     # m/s² max mechanical brake deceleration (full pedal past 0.5)
# Losses = P_elec * (1 - η_motor)  [W as heat]
MOTOR_ETA_BASE = 0.97       # Tesla M3 PMSM peak efficiency ~97% at optimal point
MOTOR_K_COPPER = 0.06       # copper loss coefficient (scales with I² ∝ throttle²)
MOTOR_K_IRON   = 0.03       # iron/eddy loss (scales with RPM)
# floor set to 0.85 — real PMSM worst case, not 0.70 (that would be ICE territory)

# Inverter: IGBT switching losses scale with current (∝ throttle) + fixed conduction loss
INVERTER_ETA_BASE = 0.98
INVERTER_K_SWITCH = 0.03    # switching loss coefficient

# Battery: Joule heating Q = I²·R_int
BATTERY_R_INTERNAL = 0.042  # Ω  Tesla M3 75kWh: 96s×46p, cell IR≈20mΩ → 96*(20mΩ/46)=0.042Ω
BATTERY_V_NOMINAL = 400.0   # V  nominal pack voltage

COOLING_POLL_INTERVAL = 5.0  # seconds between cooling state updates
class CoolingAction:
    LIQUID_WARM = -1
    NONE = 0
    PASSIVE = 1
    ACTIVE_FAN = 2
    LIQUID_COLD = 3

class ThermalDevice:
    def __init__(self, name, temp_normal, temp_optimal, temp_emergency, thermal_mass):
        self.name = name
        self.temp_normal_min,    self.temp_normal_max    = temp_normal
        self.temp_optimal_min,   self.temp_optimal_max   = temp_optimal
        self.temp_emergency_min, self.temp_emergency_max = temp_emergency
        self.thermal_mass = thermal_mass
        self.temperature = AMBIENT_TEMP
        self.cooling_action = CoolingAction.NONE
        self.heat_to_coolant = 0.0

    def is_in_emergency(self, temp):
        return temp < self.temp_emergency_min or temp > self.temp_emergency_max

    def update_temperature(self, heat_generated, ambient_temp, coolant_temp, dt):
        """Newton's law of cooling. Liquid modes cool against coolant temp, others against ambient."""
        action = self.cooling_action
        if action in (CoolingAction.LIQUID_COLD, CoolingAction.LIQUID_WARM):
            h_liquid = LIQUID_COOLING_ACTIVE if action == CoolingAction.LIQUID_COLD else LIQUID_COOLING_PASSIVE
            q_liquid = h_liquid * (self.temperature - coolant_temp)
            q_air    = NATURAL_CONVECTION * (self.temperature - ambient_temp)
            q_cool   = q_liquid + q_air
            self.heat_to_coolant = max(0.0, q_liquid)
        else:
            h_air  = NATURAL_CONVECTION + {CoolingAction.PASSIVE:    PASSIVE_COOLING,
                                            CoolingAction.ACTIVE_FAN: ACTIVE_FAN_COOLING}.get(action, 0)
            q_cool = h_air * (self.temperature - ambient_temp)
            self.heat_to_coolant = 0.0
        self.temperature += ((heat_generated - q_cool) * dt) / self.thermal_mass

    def choose_cooling_action(self, speed_kmh):
        """4-zone state machine based on optimal/normal/emergency bands:
           T <= optimal_max              -> NONE   (sweet spot, no cooling needed)
           optimal_max < T <= normal_max -> PASSIVE (fins; ram air helps at speed)
           normal_max  < T              -> escalate FAN -> LIQUID_COLD
           T > emergency_max            -> emergency flag
        """
        T = self.temperature
        if T <= self.temp_optimal_max:
            # Step cooling down — already at or below ideal ceiling
            self.cooling_action = max(CoolingAction.NONE, self.cooling_action - 1)
        elif T <= self.temp_normal_max:
            # Above optimal but within normal — passive only
            # At speed, ram air through the radiator/fins does the job
            self.cooling_action = CoolingAction.PASSIVE
        else:
            # Above normal max — escalate one step per tick
            self.cooling_action = min(self.cooling_action + 1, CoolingAction.LIQUID_COLD)
        return self.is_in_emergency(T)

class CoolantLoop:
    """Independent coolant circuit with its own thermal mass and radiator."""
    def __init__(self, ambient_temp, thermal_mass, radiator_h):
        self.temperature = ambient_temp
        self.thermal_mass = thermal_mass
        self.radiator_h = radiator_h

    def update(self, heat_absorbed, ambient_temp, speed_ms, dt):
        # Ram air boosts radiator dissipation proportional to dynamic pressure (v²)
        h_eff = self.radiator_h + RAM_AIR_K * speed_ms**2
        q_rad = h_eff * (self.temperature - ambient_temp)
        self.temperature += ((heat_absorbed - q_rad) * dt) / self.thermal_mass

class EVSimulation:
    def __init__(self):
        self.speed = 0.0  # m/s
        self.battery_energy = BATTERY_CAPACITY
        self.throttle = 0.0
        self.brake    = 0.0
        self.distance = 0.0 # meters

        # Thermal system
        self.motor_thermal    = ThermalDevice("Motor",    MOTOR_TEMP_NORMAL,    MOTOR_TEMP_OPTIMAL,    MOTOR_TEMP_EMERGENCY,    MOTOR_THERMAL_MASS)
        self.battery_thermal  = ThermalDevice("Battery",  BATTERY_TEMP_NORMAL,  BATTERY_TEMP_OPTIMAL,  BATTERY_TEMP_EMERGENCY,  BATTERY_THERMAL_MASS)
        self.inverter_thermal = ThermalDevice("Inverter", INVERTER_TEMP_NORMAL, INVERTER_TEMP_OPTIMAL, INVERTER_TEMP_EMERGENCY, INVERTER_THERMAL_MASS)
        self.coolant_pt  = CoolantLoop(AMBIENT_TEMP, COOLANT_PT_THERMAL_MASS,  COOLANT_PT_RADIATOR_H)
        self.coolant_bat = CoolantLoop(AMBIENT_TEMP, COOLANT_BAT_THERMAL_MASS, COOLANT_BAT_RADIATOR_H)
        self.ambient_temp = AMBIENT_TEMP
        self.emergency_shutdown = False
        self._cooling_poll_ticks = 0
        self._cooling_poll_every = int(COOLING_POLL_INTERVAL / DT)
        self.last_power_kw = 0.0
        self._efficiency_window = int(30.0 / DT)  # 30s rolling average for kWh/100km
        
        self.print_parameters()

    def print_parameters(self):
        print("EV Simulation Parameters:")
        print(f"  MASS: {MASS} kg")
        print(f"  MAX_WHEEL_TORQUE: {MAX_WHEEL_TORQUE} Nm")
        print(f"  MAX_POWER: {MAX_POWER} W")
        print(f"  BATTERY_CAPACITY: {BATTERY_CAPACITY / 3600000:.2f} kWh")
        print(f"  EFFICIENCY: {EFFICIENCY:.2f}")
        print(f"  WHEEL_RADIUS: {WHEEL_RADIUS:.2f} m")
        print(f"  DT: {DT:.3f} s")
        print("Resistance Factors:")
        print(f"  FRONTAL_AREA: {FRONTAL_AREA} m^2")
        print(f"  AIR_DENSITY: {AIR_DENSITY} kg/m^3")
        print(f"  DRAG_COEFF: {DRAG_COEFF:.2f}")
        print(f"  GRAVITY: {GRAVITY:.2f} m/s^2")
        print(f"  ROLLING_RESIST_COEFF: {ROLLING_RESIST_COEFF:.4f}")
        print()

    def get_parameters(self):
        return [
            ("MASS", f"{MASS} kg"),
            ("MAX_WHEEL_TORQUE", f"{MAX_WHEEL_TORQUE} Nm"),
            ("MAX_POWER", f"{MAX_POWER} W"),
            ("BATTERY_CAPACITY", f"{BATTERY_CAPACITY / 3600000:.2f} kWh"),
            ("EFFICIENCY", f"{EFFICIENCY:.2f}"),
            ("WHEEL_RADIUS", f"{WHEEL_RADIUS:.2f} m"),
            ("DT", f"{DT:.3f} s"),
            ("DRAG_COEFF", f"{DRAG_COEFF:.2f}"),
            ("ROLLING_RESIST_COEFF", f"{ROLLING_RESIST_COEFF:.4f}"),
        ]

    def update(self):
        # Check for emergency shutdown
        if self.emergency_shutdown:
            applied_torque = 0
            throttle_used = 0
        else:
            throttle_used = self.throttle
        
        v = self.speed
        speed_kmh = v * 3.6
        angular_vel_wheel = v / WHEEL_RADIUS
        motor_rpm = (angular_vel_wheel * 60 / (2 * 3.14159)) * 9.04

        # 1. HARD RPM LIMIT (The 18,000 RPM Wall)
        if motor_rpm >= 18000:
            self.speed = (18000 / 9.04) * (2 * 3.14159 * WHEEL_RADIUS / 60)
            applied_torque = 0 # Motor cannot spin faster
        else:
            # 2. POWER & BACK-EMF LIMIT
            # Tesla motors lose torque at high RPM due to Back-EMF
            if motor_rpm < 5000:
                max_avail_torque = MAX_WHEEL_TORQUE
            else:
                # Torque starts dropping significantly after 5000 motor RPM
                max_avail_torque = min(MAX_WHEEL_TORQUE, MAX_POWER / (v / WHEEL_RADIUS))
            
            applied_torque = throttle_used * max_avail_torque

        # 3. AERODYNAMIC DRAG (The 'Air Wall')
        # Force grows with the SQUARE of speed
        drag_force    = 0.5 * DRAG_COEFF * FRONTAL_AREA * AIR_DENSITY * v**2
        rolling_force = ROLLING_RESIST_COEFF * MASS * GRAVITY
        engine_force  = applied_torque / WHEEL_RADIUS

        # --- BRAKING ---
        brake_used = self.brake if not self.emergency_shutdown else 0.0
        regen_fraction = min(brake_used, 0.5) / 0.5        # 0.0-1.0 over first half of pedal
        mech_fraction  = max(0.0, brake_used - 0.5) / 0.5  # 0.0-1.0 over second half

        regen_torque  = regen_fraction * REGEN_MAX_TORQUE   # Nm at wheels (braking)
        regen_force   = regen_torque / WHEEL_RADIUS
        mech_brake_force = mech_fraction * MECH_BRAKE_DECEL * MASS

        net_force = engine_force - drag_force - rolling_force - regen_force - mech_brake_force

        acceleration = net_force / MASS
        self.speed = max(0.0, self.speed + acceleration * DT)

        # --- ENERGY ---
        if speed_kmh < 100:
            dyn_eff = 0.94
        else:
            dyn_eff = max(0.80, 0.94 - (speed_kmh - 100) / 1000)

        mech_power    = applied_torque * (v / WHEEL_RADIUS)
        electrical_draw = (mech_power / dyn_eff) + 500      # 500W overhead (drive mode)

        # Regen: kinetic energy recovered into battery
        regen_power   = regen_force * v                     # mechanical power absorbed
        regen_returned = regen_power * REGEN_EFFICIENCY     # W back into battery

        # --- THERMAL: drive heat ---
        rpm_norm  = min(motor_rpm / 18000, 1.0)
        eta_motor = max(0.85, MOTOR_ETA_BASE - MOTOR_K_COPPER * throttle_used**2 - MOTOR_K_IRON * rpm_norm)
        motor_heat = mech_power * (1.0 - eta_motor)

        eta_inv    = max(0.88, INVERTER_ETA_BASE - INVERTER_K_SWITCH * throttle_used)
        inverter_heat = mech_power * (1.0 - eta_inv)

        # Regen also heats motor/inverter (reversed current, same loss model)
        motor_heat    += regen_power * (1.0 - eta_motor)
        inverter_heat += regen_power * (1.0 - eta_inv)

        battery_current = electrical_draw / BATTERY_V_NOMINAL
        battery_heat    = battery_current**2 * BATTERY_R_INTERNAL
        
        # Motor + Inverter → powertrain loop; Battery → battery loop
        ct_pt  = self.coolant_pt.temperature
        ct_bat = self.coolant_bat.temperature
        self.motor_thermal.update_temperature(motor_heat,    self.ambient_temp, ct_pt,  DT)
        self.inverter_thermal.update_temperature(inverter_heat, self.ambient_temp, ct_pt,  DT)
        self.battery_thermal.update_temperature(battery_heat,  self.ambient_temp, ct_bat, DT)

        self.coolant_pt.update(
            self.motor_thermal.heat_to_coolant + self.inverter_thermal.heat_to_coolant,
            self.ambient_temp, v, DT)
        self.coolant_bat.update(
            self.battery_thermal.heat_to_coolant,
            self.ambient_temp, v, DT)

        # Update cooling actions every 5s — no need to change cooling mode 20x/second
        self._cooling_poll_ticks += 1
        if self._cooling_poll_ticks >= self._cooling_poll_every:
            self._cooling_poll_ticks = 0
            motor_emergency    = self.motor_thermal.choose_cooling_action(speed_kmh)
            inverter_emergency = self.inverter_thermal.choose_cooling_action(speed_kmh)
            battery_emergency  = self.battery_thermal.choose_cooling_action(speed_kmh)
            if motor_emergency or inverter_emergency or battery_emergency:
                self.emergency_shutdown = True
            else:
                self.emergency_shutdown = False
        
        # Battery energy drain
        self.battery_energy -= electrical_draw * DT
        self.battery_energy += regen_returned * DT
        self.battery_energy  = min(self.battery_energy, BATTERY_CAPACITY)  # cap at full
        self.last_power_kw = (electrical_draw - regen_returned) / 1000.0
        # kWh/100km = avg(kW) / avg(km/h) * 100  — stored as running sums for rolling window
        self._eff_power_sum  = getattr(self, '_eff_power_sum',  0.0) + max(0.0, self.last_power_kw)
        self._eff_speed_sum  = getattr(self, '_eff_speed_sum',  0.0) + (v * 3.6)
        self._eff_ticks      = getattr(self, '_eff_ticks',      0)   + 1
        if self._eff_ticks > self._efficiency_window:
            # drop oldest sample — approximate with decay instead of full deque for simplicity
            self._eff_power_sum *= (self._efficiency_window / (self._efficiency_window + 1))
            self._eff_speed_sum *= (self._efficiency_window / (self._efficiency_window + 1))
            self._eff_ticks      = self._efficiency_window
        avg_speed = self._eff_speed_sum / max(1, self._eff_ticks)
        avg_power = self._eff_power_sum / max(1, self._eff_ticks)
        self.last_efficiency_kwh100 = (avg_power / avg_speed * 100) if avg_speed > 2.0 else None
        self.distance += self.speed * DT

        return (self.speed * 3.6, self.battery_energy / 3600000, self.distance / 1000,
                self.motor_thermal.temperature, self.battery_thermal.temperature,
                self.inverter_thermal.temperature,
                self.coolant_pt.temperature, self.coolant_bat.temperature,
                self.emergency_shutdown)
class EVGUI:
    def __init__(self, root):
        self.sim = EVSimulation()
        self.root = root
        self.root.title("EV prototype - System Monitor")
        self.root.configure(padx=20, pady=20)

        style = ttk.Style()
        style.configure("TLabel", font=("Helvetica", 12))
        style.configure("Header.TLabel", font=("Helvetica", 16, "bold"))
        style.configure("Param.TLabelframe.Label", font=("Helvetica", 12, "bold"))

        ttk.Label(root, text="POWERTRAIN MONITOR", style="Header.TLabel").pack(pady=(0, 8))

        # ── Two-column container ──────────────────────────────────────────────
        columns = ttk.Frame(root)
        columns.pack(fill="both", expand=True)

        # ── LEFT COLUMN: controls + motion ───────────────────────────────────
        left = ttk.Frame(columns)
        left.pack(side="left", fill="both", expand=True, padx=(0, 15))

        self.throttle_var = tk.DoubleVar(value=0.0)
        self.throttle_label = ttk.Label(left, text="Throttle: 0%", width=32)
        self.throttle_label.pack(anchor="w")
        throttle_slider = ttk.Scale(left, from_=0.0, to=1.0, orient="horizontal",
                  variable=self.throttle_var, command=self.on_throttle_change)
        throttle_slider.pack(fill="x")
        throttle_slider.configure(length=300)

        self.brake_var = tk.DoubleVar(value=0.0)
        self.brake_label = ttk.Label(left, text="Brake: 0%  [regen off]", width=32)
        self.brake_label.pack(anchor="w", pady=(6, 0))
        brake_slider = ttk.Scale(left, from_=0.0, to=1.0, orient="horizontal",
                  variable=self.brake_var, command=self.on_brake_change)
        brake_slider.pack(fill="x")
        brake_slider.configure(length=300)

        params_frame = ttk.LabelFrame(left, text="EV Parameters", style="Param.TLabelframe")
        params_frame.pack(fill="x", pady=8)
        for name, value in self.sim.get_parameters():
            ttk.Label(params_frame, text=f"{name}: {value}", font=("Helvetica", 10)).pack(anchor="w", padx=8)

        self.speed_ui = ttk.Label(left, text="0.0 km/h", font=("Courier", 26, "bold"))
        self.speed_ui.pack(pady=(10, 4))
        self.battery_ui = ttk.Label(left, text="Battery: 75.00 kWh")
        self.battery_ui.pack(anchor="w")
        self.dist_ui = ttk.Label(left, text="Distance: 0.000 km")
        self.dist_ui.pack(anchor="w")

        # Power diagram
        CHART_W, CHART_H = 300, 80
        HISTORY_TICKS = int(60.0 / DT)  # 1200 ticks = 60 seconds
        self._power_history = [0.0] * HISTORY_TICKS
        self._chart_w = CHART_W
        self._chart_h = CHART_H
        self._history_ticks = HISTORY_TICKS
        self._max_kw = MAX_POWER / 1000.0  # 220 kW scale

        power_frame = ttk.LabelFrame(left, text="Power (kW)  ■ consume  ■ regen", style="Param.TLabelframe")
        power_frame.pack(fill="x", pady=(8, 0))
        self.power_label = ttk.Label(power_frame, text="Now:   0.0 kW", font=("Courier", 11, "bold"))
        self.power_label.pack(anchor="w", padx=6)
        self.power_canvas = tk.Canvas(power_frame, width=CHART_W, height=CHART_H,
                                      bg="#111111", highlightthickness=0)
        self.power_canvas.pack(padx=6, pady=(0, 6))
        # zero line
        self._zero_y = CHART_H // 2
        self.power_canvas.create_line(0, self._zero_y, CHART_W, self._zero_y,
                                      fill="#444444", width=1)

        self.emergency_ui = ttk.Label(left, text="✓ System Normal",
                                      foreground="green", font=("Helvetica", 12, "bold"))
        self.emergency_ui.pack(pady=(12, 0))

        # ── RIGHT COLUMN: thermal ─────────────────────────────────────────────
        right = ttk.Frame(columns)
        right.pack(side="left", fill="both", expand=True)

        ttk.Label(right, text="THERMAL MONITOR", style="Header.TLabel").pack(anchor="w")

        self.ambient_label = ttk.Label(right, text=f"Ambient: {AMBIENT_TEMP:.1f}°C")
        self.ambient_label.pack(anchor="w", pady=(6, 0))
        self.ambient_var = tk.DoubleVar(value=AMBIENT_TEMP)
        ttk.Scale(right, from_=-20.0, to=50.0, orient="horizontal",
                  variable=self.ambient_var, command=self.on_ambient_change).pack(fill="x")

        # Component temps
        temps_frame = ttk.LabelFrame(right, text="Temperatures", style="Param.TLabelframe")
        temps_frame.pack(fill="x", pady=8)

        self.motor_temp_ui    = ttk.Label(temps_frame, text="Motor:      25.0°C", font=("Courier", 13, "bold"))
        self.battery_temp_ui  = ttk.Label(temps_frame, text="Battery:    25.0°C", font=("Courier", 13, "bold"))
        self.inverter_temp_ui = ttk.Label(temps_frame, text="Inverter:   25.0°C", font=("Courier", 13, "bold"))
        self.coolant_pt_ui    = ttk.Label(temps_frame, text="Coolant PT: 25.0°C", font=("Courier", 13, "bold"))
        self.coolant_bat_ui   = ttk.Label(temps_frame, text="Coolant Bat:25.0°C", font=("Courier", 13, "bold"))
        for w in (self.motor_temp_ui, self.inverter_temp_ui, self.coolant_pt_ui,
                  self.battery_temp_ui, self.coolant_bat_ui):
            w.pack(anchor="w", padx=8, pady=2)

        # Cooling actions
        cool_frame = ttk.LabelFrame(right, text="Cooling Actions", style="Param.TLabelframe")
        cool_frame.pack(fill="x", pady=4)

        self.cooling_motor_ui    = ttk.Label(cool_frame, text="Motor:    NONE")
        self.cooling_inverter_ui = ttk.Label(cool_frame, text="Inverter: NONE")
        self.cooling_battery_ui  = ttk.Label(cool_frame, text="Battery:  NONE")
        for w in (self.cooling_motor_ui, self.cooling_inverter_ui, self.cooling_battery_ui):
            w.pack(anchor="w", padx=8, pady=2)

        # ── SCHEMATIC PANEL: car image with live temp overlays ────────────────
        schema_frame = ttk.LabelFrame(root)
        schema_frame.pack(fill="x", pady=(12, 0))

        IMG_W, IMG_H = 1511, 704
        sx, sy = 2, 2
        DISP_W = IMG_W // sx   # 755
        DISP_H = IMG_H // sy   # 352

        img_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                "powertrain_scheme2.png")
        raw = tk.PhotoImage(file=img_path)
        self._schema_img = raw.subsample(sx, sy)

        self.canvas = tk.Canvas(schema_frame, width=DISP_W, height=DISP_H,
                                bg="#1a1a1a", highlightthickness=0)
        self.canvas.pack()
        self.canvas.create_image(0, 0, anchor="nw", image=self._schema_img)

        # Overlay anchors as % of display width, pinned near bottom of image
        BY = DISP_H - 60  # top of label block
        LH = 22           # line height
        X1 = int(DISP_W * 0.15)  # coolant/outside block
        X2 = int(DISP_W * 0.4)  # battery
        X3 = int(DISP_W * 0.59)  # inverter
        X4 = int(DISP_W * 0.73)  # motor

        def _label(x, y, text):
            """Static grey label with value text immediately after."""
            lbl = self.canvas.create_text(x, y, anchor="w", text=text,
                                          fill="#aaaaaa", font=("Helvetica", 12))
            # measure actual rendered width and place value right after
            x2 = self.canvas.bbox(lbl)[2] + 4
            return self.canvas.create_text(x2, y, anchor="w", text="--.-°C",
                                           fill="#00ff99", font=("Courier", 13, "bold"))

        self._ov_outside  = _label(X1, BY,          "OUTSIDE:")
        self._ov_cbat     = _label(X1, BY + LH,     "BAT COOLANT:")
        self._ov_cpt      = _label(X1, BY + LH * 2, "M/I COOLANT:")
        self._ov_battery  = _label(X2, BY + LH,     "BATTERY:")
        self._ov_inverter = _label(X3, BY + LH,     "INV:")
        self._ov_motor    = _label(X4, BY + LH,     "MOTOR:")

        self.root.update_idletasks()
        self.root.geometry(f"{self.root.winfo_width()}x{self.root.winfo_height()}")
        self.run_sim()

    def on_throttle_change(self, _):
        val = self.throttle_var.get()
        self.sim.throttle = val
        self.throttle_label.config(text=f"Throttle: {val*100:.0f}%")
        if val > 0.0:
            self.brake_var.set(0.0)
            self.sim.brake = 0.0
            self.brake_label.config(text="Brake: 0%  [regen off]")

    def on_brake_change(self, _):
        val = self.brake_var.get()
        self.sim.brake = val
        if val > 0.0:
            self.throttle_var.set(0.0)
            self.sim.throttle = 0.0
            self.throttle_label.config(text="Throttle: 0%")
        if val <= 0.0:
            mode = "regen off"
        elif val <= 0.5:
            pct = val / 0.5 * 100
            mode = f"regen {pct:.0f}%"
        else:
            regen_pct = 100
            mech_pct  = (val - 0.5) / 0.5 * 100
            mode = f"regen 100% + disk {mech_pct:.0f}%"
        self.brake_label.config(text=f"Brake: {val*100:.0f}%  [{mode}]")

    def on_ambient_change(self, _):
        ambient = self.ambient_var.get()
        self.sim.ambient_temp = ambient
        self.ambient_label.config(text=f"Ambient: {ambient:.1f}°C")
        self.sim.motor_thermal.temperature = ambient
        self.sim.battery_thermal.temperature = ambient
        self.sim.inverter_thermal.temperature = ambient
        self.sim.coolant_pt.temperature  = ambient
        self.sim.coolant_bat.temperature = ambient

    def run_sim(self):
        speed_kmh, kwh_left, dist_km, motor_temp, battery_temp, inverter_temp, coolant_pt_temp, coolant_bat_temp, emergency = self.sim.update()

        self.speed_ui.config(text=f"{speed_kmh:.1f} km/h")
        self.battery_ui.config(text=f"Battery: {kwh_left:.2f} kWh")
        self.dist_ui.config(text=f"Distance: {dist_km:.3f} km")

        # Power chart update
        kw = self.sim.last_power_kw
        self._power_history.append(kw)
        if len(self._power_history) > self._history_ticks:
            self._power_history.pop(0)

        eff = self.sim.last_efficiency_kwh100
        eff_str = f"{eff:.1f} kWh/100km" if eff is not None else "-- kWh/100km"
        self.power_label.config(
            text=f"Now: {kw:+7.1f} kW    {eff_str}",
            foreground="#00cc44" if kw < 0 else "#ff6600")

        # Redraw chart — one vertical bar per pixel column
        c = self.power_canvas
        c.delete("bar")
        W, H = self._chart_w, self._chart_h
        z = self._zero_y
        n = len(self._power_history)
        step = max(1, n // W)  # samples per pixel
        for px in range(W):
            idx = int(px * n / W)
            val = self._power_history[min(idx, n - 1)]
            # clamp to scale
            ratio = max(-1.0, min(1.0, val / self._max_kw))
            y = z - int(ratio * z)  # above zero = consume, below = regen
            if val >= 0:
                color = "#cc3300" if val > self._max_kw * 0.8 else "#ff6600"
                c.create_line(px, z, px, y, fill=color, tags="bar")
            else:
                c.create_line(px, z, px, y, fill="#00aa44", tags="bar")
        # redraw zero line on top
        c.create_line(0, z, W, z, fill="#444444", width=1, tags="bar")

        def temp_color(t, normal_max, emergency_max):
            if t > emergency_max:  return "red"
            if t > normal_max:     return "orange"
            return "black"

        self.motor_temp_ui.config(
            text=f"Motor:      {motor_temp:.1f}°C",
            foreground=temp_color(motor_temp, MOTOR_TEMP_NORMAL[1], MOTOR_TEMP_EMERGENCY[1]))
        self.battery_temp_ui.config(
            text=f"Battery:    {battery_temp:.1f}°C",
            foreground=temp_color(battery_temp, BATTERY_TEMP_NORMAL[1], BATTERY_TEMP_EMERGENCY[1]))
        self.inverter_temp_ui.config(
            text=f"Inverter:   {inverter_temp:.1f}°C",
            foreground=temp_color(inverter_temp, INVERTER_TEMP_NORMAL[1], INVERTER_TEMP_EMERGENCY[1]))
        self.coolant_pt_ui.config(text=f"Coolant PT: {coolant_pt_temp:.1f}°C")
        self.coolant_bat_ui.config(text=f"Coolant Bat:{coolant_bat_temp:.1f}°C")

        cooling_names = {-1: "LIQUID WARM", 0: "NONE", 1: "PASSIVE", 2: "ACTIVE FAN", 3: "LIQUID COLD"}
        self.cooling_motor_ui.config(
            text=f"Motor:    {cooling_names.get(self.sim.motor_thermal.cooling_action, '?')}")
        self.cooling_battery_ui.config(
            text=f"Battery:  {cooling_names.get(self.sim.battery_thermal.cooling_action, '?')}")
        self.cooling_inverter_ui.config(
            text=f"Inverter: {cooling_names.get(self.sim.inverter_thermal.cooling_action, '?')}")

        if emergency:
            self.emergency_ui.config(text="⚠ EMERGENCY SHUTDOWN", foreground="red")
        else:
            self.emergency_ui.config(text="✓ System Normal", foreground="green")

        # Schematic overlay update
        def ov_color(t, device):
            if t > device.temp_emergency_max:  return "#cc0000"   # red - out of range
            action = device.cooling_action
            if action == CoolingAction.LIQUID_COLD or action == CoolingAction.LIQUID_WARM:
                return "#cc6600"   # dark orange - liquid cooling active
            if action == CoolingAction.ACTIVE_FAN or action == CoolingAction.PASSIVE:
                return "#999900"   # dark yellow - fan/passive cooling
            return "#006600"       # dark green - no cooling needed

        self.canvas.itemconfig(self._ov_outside,  text=f"{self.sim.ambient_temp:.1f}°C")
        self.canvas.itemconfig(self._ov_motor,
            text=f"{motor_temp:.1f}°C",
            fill=ov_color(motor_temp, self.sim.motor_thermal))
        self.canvas.itemconfig(self._ov_inverter,
            text=f"{inverter_temp:.1f}°C",
            fill=ov_color(inverter_temp, self.sim.inverter_thermal))
        self.canvas.itemconfig(self._ov_battery,
            text=f"{battery_temp:.1f}°C",
            fill=ov_color(battery_temp, self.sim.battery_thermal))
        self.canvas.itemconfig(self._ov_cpt,  text=f"{coolant_pt_temp:.1f}°C")
        self.canvas.itemconfig(self._ov_cbat, text=f"{coolant_bat_temp:.1f}°C")

        self.root.after(50, self.run_sim)

if __name__ == "__main__":
    root = tk.Tk()
    app = EVGUI(root)
    root.mainloop()