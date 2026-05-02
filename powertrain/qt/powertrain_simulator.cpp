#include "powertrain_simulator.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <QString>

EVPowertrainSimulator::EVPowertrainSimulator() {
    batteryEnergy = batteryCapacity;
    currentRollingResistCoeff = ASPHALT_ROLLING_RESIST_COEFF;
    currentDragCoeffMultiplier = ASPHALT_DRAG_COEFF_MULTIPLIER;
}

void EVPowertrainSimulator::update(double dt) {
    tripTime += dt;
    modeSwitchTimer += dt;

    // 0. Environment: Dynamic Air Density (Ideal Gas Law: rho = P / (R*T))
    // Cold air is denser (more drag), hot air is thinner (less drag)
    double airDensity = AIR_PRESSURE_SEA_LEVEL / (SPECIFIC_GAS_CONST_AIR * (ambientTemp + 273.15));

    // 1. Auxiliary Power Budgeting
    // In a real car, the motor only gets what the battery has left after cabin systems
    double ac_power = 0.0;
    if (acOn) {
        double temp_diff = std::abs(ambientTemp - acTargetTemp);
        ac_power = std::clamp(temp_diff * 250.0, 500.0, 5000.0);
    }
    double light_power = highBeamsOn ? 250.0 : (lowBeamsOn ? 100.0 : 0.0);
    double aux_load = ac_power + SYSTEM_BASE_LOAD + infotainmentPowerDraw + light_power;

    // If Emergency: strictly lock to 20%. Otherwise: apply Thermal Derate and Drive Mode.
    double modeMultiplier = emergencyShutdown ? 0.20 : (thermalDerate ? 0.70 : 1.0) * driveModeLimit;
    
    // Total available power from battery after mode restrictions and aux loads
    double available_battery_power = (maxPower * modeMultiplier) - aux_load;
    // Net power available for propulsion (accounting for approx 94% motor/inv efficiency)
    double propulsion_limit = std::max(0.0, available_battery_power * 0.94);

    // Slew Rate Limiter: 3.0s for 0.3 power change (100% -> 70%) -> 0.1 units/sec
    double maxStep = 0.1 * dt; 
    static double smoothLimit = 1.0; 
    if (smoothLimit < modeMultiplier)
        smoothLimit = std::min(modeMultiplier, smoothLimit + maxStep);
    else if (smoothLimit > modeMultiplier)
        smoothLimit = std::max(modeMultiplier, smoothLimit - maxStep);

    double powerLimit = smoothLimit;

    // 1. Calculate Drive Torque (Constant Torque -> Constant Power)
    double angular_vel = speed / wheelRadius;
    double motor_rpm = (angular_vel * 60.0 / (2.0 * M_PI)) * 9.04;
    
    double max_avail_torque = (motor_rpm < 5000) ? 
        maxWheelTorque : std::min(maxWheelTorque, propulsion_limit / std::max(0.01, speed / wheelRadius));
    
    // 1a. RPM Safety Limiter (Tesla RDU structural limit at 18,000 RPM)
    if (motor_rpm > 17800.0) {
        double rpm_limit_factor = std::max(0.0, (18000.0 - motor_rpm) / 200.0);
        max_avail_torque *= rpm_limit_factor;
    }

    double engine_force = (throttle * powerLimit * max_avail_torque) / wheelRadius;

    // 2. Resistance Forces
    // Relative velocity determines drag: +windSpeed is a tailwind (reduces relative velocity)
    double v_rel = speed - windSpeed;
    double drag_force = 0.5 * (baseDragCoeff * currentDragCoeffMultiplier) * FRONTAL_AREA * airDensity * v_rel * std::abs(v_rel);

    double rolling_force = currentRollingResistCoeff * mass * 9.81;
    double gradient_force = mass * 9.81 * std::sin(gradient * M_PI / 180.0);

    // 3. Braking Logic
    double regen_torque = std::min(brake, 0.5) / 0.5 * 1800.0;
    double regen_force = regen_torque / wheelRadius;
    double mech_brake_force = std::max(0.0, brake - 0.5) / 0.5 * 8.0 * mass;

    double net_force = engine_force - drag_force - rolling_force - regen_force - mech_brake_force - gradient_force;
    
    // 4. Physics Integration
    double acceleration = net_force / mass;
    speed = std::max(0.0, speed + acceleration * dt);
    distance += speed * dt;

    // 5. Energy Consumption
    double mech_power = (throttle * powerLimit * max_avail_torque) * (speed / wheelRadius);
    double speed_kmh = speed * 3.6;    

    // Simple Voltage-Drop Model based on SOC
    double soc = batteryEnergy / batteryCapacity;
    // Voltage ranges from ~420V (Full) to ~320V (Empty)
    double current_voltage = 320.0 + (100.0 * soc);
    
    // Detailed Propulsion Efficiency (from Master Spec)
    double motor_rpm_norm = std::min(motor_rpm / 18000.0, 1.0);
    double motor_eff = std::max(0.70, MOTOR_ETA_BASE - (MOTOR_K_COPPER * std::pow(throttle, 2)) - (MOTOR_K_IRON * motor_rpm_norm));
    double inv_eff = std::max(0.85, INV_ETA_BASE - (INV_K_SWITCH * throttle));

    // Total electrical draw: mech_power corrected by inverter/motor efficiency + cabin aux
    double electrical_draw = (mech_power / (motor_eff * inv_eff)) + aux_load;

    // Battery Heat based on current (I = P/V)
    double battery_current = electrical_draw / current_voltage;
    // Joule Heating: Q = I^2 * R. Internal Resistance roughly 0.042 Ohms.
    double battery_heat = std::pow(battery_current, 2) * 0.042;

    double regen_power = regen_force * speed;
    double regen_returned = regen_power * 0.85;

    batteryEnergy -= electrical_draw * dt;
    batteryEnergy += regen_returned * dt;
    batteryEnergy = std::min(batteryEnergy, batteryCapacity);
    
    lastPowerKw = (electrical_draw - regen_returned) / 1000.0;

    // Efficiency calculation (30s rolling average approximation)
    effPowerSum += std::max(0.0, lastPowerKw);
    effSpeedSum += speed_kmh;
    effTicks++;

    if (effTicks > 600) { // 30s at 20Hz (50ms DT)
        effPowerSum *= (600.0 / 601.0);
        effSpeedSum *= (600.0 / 601.0);
        effTicks = 600;
    }
    double avgSpeed = effSpeedSum / std::max(1, effTicks);
    double avgPower = effPowerSum / std::max(1, effTicks);
    lastEfficiencyKwh100 = (avgSpeed > 2.0) ? (avgPower / avgSpeed * 100.0) : 0.0;

    // 6. Thermal Modeling
    // Heat is generated by losses in the conversion
    double motor_heat = (std::abs(mech_power) + std::abs(regen_power)) * (1.0 - motor_eff);
    double inv_heat = (std::abs(mech_power) + std::abs(regen_power)) * (1.0 - inv_eff);

    auto calculateDissipation = [&](double temp, double refCoolantTemp, CoolingAction action, double& heatToCoolant) {
        double q_air_base = NATURAL_CONVECTION * (temp - ambientTemp);
        heatToCoolant = 0.0;

        if (action == CoolingAction::LIQUID_COLD || action == CoolingAction::LIQUID_WARM) {
            double h_liq = (action == CoolingAction::LIQUID_COLD) ? LIQUID_COOLING_ACTIVE : LIQUID_COOLING_PASSIVE;
            double q_liq = h_liq * (temp - refCoolantTemp);
            heatToCoolant = std::max(0.0, q_liq); // Only positive transfer heats the coolant loop
            return q_air_base + q_liq;
        } else {
            double h_extra = (action == CoolingAction::TURNED_OFF) ? PASSIVE_COOLING : 
                             (action == CoolingAction::ACTIVE_FAN) ? ACTIVE_FAN_COOLING : 0.0;
            return q_air_base + (h_extra * (temp - ambientTemp));
        }
    };

    double mToPT, iToPT, bToBat;
    double mDiss = calculateDissipation(motorTemp, coolantPTTemp, motorAction, mToPT);
    double iDiss = calculateDissipation(inverterTemp, coolantPTTemp, inverterAction, iToPT);
    double bDiss = calculateDissipation(batteryTemp, coolantBatTemp, batteryAction, bToBat);

    motorTemp += ((motor_heat - mDiss) * dt) / motorThermalMass;
    inverterTemp += ((inv_heat - iDiss) * dt) / inverterThermalMass;
    batteryTemp += ((battery_heat - bDiss) * dt) / batteryThermalMass;

    // 7. Coolant Loop Physics (Radiator + Ram Air)
    // Radiator cooling depends on the magnitude of relative air velocity
    double h_ram_boost = RAM_AIR_K * v_rel * v_rel;
    
    // Powertrain Loop (Motor + Inverter)
    double pt_rad_diss = (COOLANT_PT_RAD_H + h_ram_boost) * (coolantPTTemp - ambientTemp);
    coolantPTTemp += ((mToPT + iToPT - pt_rad_diss) * dt) / COOLANT_PT_MASS;

    // Battery Loop
    double bat_rad_diss = (COOLANT_BAT_RAD_H + h_ram_boost) * (coolantBatTemp - ambientTemp);
    coolantBatTemp += ((bToBat - bat_rad_diss) * dt) / COOLANT_BAT_MASS;

    // 8. Cooling System State Machine (Polled every 5s)
    coolingPollTimer += dt;
    if (coolingPollTimer >= 5.0) {
        coolingPollTimer = 0.0;
        updateDeviceCooling(motorTemp, MOTOR_OPTIMAL_MAX, MOTOR_NORMAL_MAX, motorAction);
        updateDeviceCooling(inverterTemp, INV_OPTIMAL_MAX, INV_NORMAL_MAX, inverterAction);
        updateDeviceCooling(batteryTemp, BAT_OPTIMAL_MAX, BAT_NORMAL_MAX, batteryAction);

        // Emergency if too hot OR too cold
        bool wantsEmergency = (motorTemp > MOTOR_CRITICAL || motorTemp < MOTOR_COLD_LIMIT ||
                               inverterTemp > INV_CRITICAL || inverterTemp < INV_COLD_LIMIT ||
                               batteryTemp > BAT_CRITICAL || batteryTemp < BAT_COLD_LIMIT);
        
        if (wantsEmergency && !emergencyShutdown) {
            if (motorTemp > MOTOR_CRITICAL) spdlog::critical("Emergency Shutdown: Motor Overheat ({} C)", motorTemp);
            if (inverterTemp > INV_CRITICAL) spdlog::critical("Emergency Shutdown: Inverter Overheat ({} C)", inverterTemp);
            if (batteryTemp > BAT_CRITICAL) spdlog::critical("Emergency Shutdown: Battery Overheat ({} C)", batteryTemp);
        }

        bool wantsDerate = (motorTemp > MOTOR_NORMAL_MAX || inverterTemp > INV_NORMAL_MAX || batteryTemp > BAT_NORMAL_MAX);

        if (wantsDerate && !thermalDerate && !wantsEmergency) {
            QString derateReason;
            if (motorTemp > MOTOR_NORMAL_MAX) {
                derateReason += QString::asprintf("Motor (%.1f°C) ", motorTemp);
            }
            if (inverterTemp > INV_NORMAL_MAX) {
                derateReason += QString::asprintf("Inverter (%.1f°C) ", inverterTemp);
            }
            if (batteryTemp > BAT_NORMAL_MAX) {
                derateReason += QString::asprintf("Battery (%.1f°C) ", batteryTemp);
            }
            spdlog::warn("Thermal Derate Active: System exceeding normal operating thresholds. Affected units: {}", derateReason.toStdString());
        }

        // Apply 30s Hysteresis Cooldown for mode switching stability
        if (wantsEmergency != emergencyShutdown || (wantsDerate != thermalDerate && !wantsEmergency)) {
            if (modeSwitchTimer >= 30.0) {
                if (emergencyShutdown && !wantsEmergency) spdlog::info("Safety Systems Clear: Emergency Shutdown Lifted");
                if (thermalDerate && !wantsDerate) spdlog::info("Temperatures Stabilized: Thermal Derate Lifted");

                emergencyShutdown = wantsEmergency;
                if (emergencyShutdown) {
                    thermalDerate = false;
                } else {
                    thermalDerate = wantsDerate;
                }
                modeSwitchTimer = 0.0; // Lock the new state for 30 seconds
            }
        }
    }
}

void EVPowertrainSimulator::updateDeviceCooling(double temp, double optimalMax, double normalMax, CoolingAction &action) {
    if (temp <= optimalMax) {
        // De-escalate cooling
        if (action == CoolingAction::LIQUID_COLD) {
            action = CoolingAction::ACTIVE_FAN;
        } else if (action == CoolingAction::ACTIVE_FAN) {
            action = CoolingAction::TURNED_OFF;
        } else { // LIQUID_WARM or TURNED_OFF
            action = CoolingAction::TURNED_OFF;
        }
    } else if (temp <= normalMax) {
        // Within normal range, but above optimal.
        // If currently in an active cooling state (ACTIVE_FAN, LIQUID_COLD), de-escalate to TURNED_OFF.
        // If already TURNED_OFF or LIQUID_WARM, stay there.
        if (action == CoolingAction::ACTIVE_FAN || action == CoolingAction::LIQUID_COLD) {
            action = CoolingAction::TURNED_OFF;
        }
    } else { // temp > normalMax
        // Escalate cooling
        if (action == CoolingAction::TURNED_OFF) {
            action = CoolingAction::ACTIVE_FAN;
        } else if (action == CoolingAction::ACTIVE_FAN) {
            action = CoolingAction::LIQUID_COLD;
        } else { // LIQUID_COLD or LIQUID_WARM
            action = CoolingAction::LIQUID_COLD;
        }
    }
}

void EVPowertrainSimulator::setSurfaceType(int typeIndex) {
    switch (typeIndex) {
        case 0: // Asphalt
            currentRollingResistCoeff = ASPHALT_ROLLING_RESIST_COEFF;
            currentDragCoeffMultiplier = ASPHALT_DRAG_COEFF_MULTIPLIER;
            break;
        case 1: // Gravel
            currentRollingResistCoeff = GRAVEL_ROLLING_RESIST_COEFF;
            currentDragCoeffMultiplier = GRAVEL_DRAG_COEFF_MULTIPLIER;
            break;
        case 2: // Ice
            currentRollingResistCoeff = ICE_ROLLING_RESIST_COEFF;
            currentDragCoeffMultiplier = 1.0; // Ice doesn't significantly change drag
            break;
        default: // Default to Asphalt
            currentRollingResistCoeff = ASPHALT_ROLLING_RESIST_COEFF;
            currentDragCoeffMultiplier = ASPHALT_DRAG_COEFF_MULTIPLIER;
            break;
    }
}

void EVPowertrainSimulator::setAmbientTemp(double t) {
    ambientTemp = t;
    // Apply equilibrium only if vehicle is at rest AND has not yet started its journey
    if (speed < 0.1 && distance < 0.001) { 
        motorTemp = t;
        inverterTemp = t;
        batteryTemp = t;
        coolantPTTemp = t;
        coolantBatTemp = t;
    }
}

void EVPowertrainSimulator::setDriveMode(int index) {
    driveMode = static_cast<DriveMode>(index);
    
    std::string modeStr;
    switch(driveMode) {
        case DriveMode::ECONOMIC: modeStr = "ECONOMIC"; break;
        case DriveMode::NORMAL:   modeStr = "NORMAL"; break;
        case DriveMode::SPORT:    modeStr = "SPORT"; break;
    }
    spdlog::info("Drive Mode Switch: {}", modeStr);

    switch(driveMode) {
        case DriveMode::ECONOMIC: driveModeLimit = 0.5; break;
        case DriveMode::NORMAL:   driveModeLimit = 0.75; break;
        case DriveMode::SPORT:    driveModeLimit = 1.0; break;
    }
    // Force re-calculation of slew rate towards new limit
}