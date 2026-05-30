#include "powertrain_simulator.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <QString>

EVPowertrainSimulator::EVPowertrainSimulator() {
    batteryEnergy = batteryCapacity;
    currentRollingResistCoeff = ASPHALT_ROLLING_RESIST_COEFF;
    currentDragCoeffMultiplier = ASPHALT_DRAG_COEFF_MULTIPLIER;
}

void EVPowertrainSimulator::setIgnition(bool on) {
    // Safety: prevent stopping the high-voltage system while the vehicle is in motion (> 1 km/h)
    if (!on && speed > 0.28) { 
        spdlog::warn("Ignition stop rejected: Safety lock active while vehicle in motion ({:.1f} km/h)", speed * 3.6);
        return;
    }

    ignitionOn = on;
    spdlog::info("Ignition set to: {}", on ? "ON" : "OFF");

    // Reset thermal management flags when turning off
    if (!on) {
        batteryHeaterOn = false;
        batteryChillerOn = false;
        ptHeaterOn = false;
        ptChillerOn = false;
        precheckStatus = PrecheckStatus::Initializing;
    }
}

void EVPowertrainSimulator::update(double dt) {
    tripTime += dt;
    modeSwitchTimer += dt;

    // 0. Environment: Dynamic Air Density (Ideal Gas Law: rho = P / (R*T))
    // Cold air is denser (more drag), hot air is thinner (less drag)
    double airDensity = AIR_PRESSURE_SEA_LEVEL / (SPECIFIC_GAS_CONST_AIR * (ambientTemp + 273.15));

    // 1. Auxiliary Power Budgeting (12V vs High Voltage)
    // LV systems are available as soon as we "jump into the car"
    double lv_load = SYSTEM_BASE_LOAD + infotainmentPowerDraw;
    double light_power = highBeamsOn ? 250.0 : (lowBeamsOn ? 100.0 : 0.0);
    lv_load += light_power;

    double hv_aux_load = 0.0;          // Pulls from Traction Pack

    // AC Compressor is high-voltage (HV) and available in standby/ACC
    // In our single button logic, we'll allow AC to run if ignition is requested or always on
    if (acOn) {
        double temp_diff = std::abs(ambientTemp - acTargetTemp);
        hv_aux_load += std::clamp(temp_diff * 250.0, 500.0, 5000.0);
    }

    // 1a. 12V System Maintenance (DC-DC Converter)
    // If 12V battery is low, DC-DC pulls from HV Traction pack to charge it
    // Implement hysteresis: Start at 80%, Stop at 98% to prevent rapid oscillation (blinking)
    if (battery12VEnergy < (battery12VCapacity * 0.80)) {
        dcdcActive = true;
    } else if (battery12VEnergy >= (battery12VCapacity * 0.98)) {
        dcdcActive = false;
    }

    if (dcdcActive) {
        double charging_power = DCDC_MAX_POWER; 
        hv_aux_load += (charging_power / 0.92); // Accounting for 92% DC-DC efficiency
        battery12VEnergy += charging_power * dt;
    }
    battery12VEnergy -= lv_load * dt;
    battery12VEnergy = std::clamp(battery12VEnergy, 0.0, battery12VCapacity);

    // 1b. High-Voltage Thermal Management (ONLY active during Ignition)
    if (ignitionOn) {
        if (batteryHeaterOn) hv_aux_load += batteryHeaterPowerDraw;
        if (batteryChillerOn) hv_aux_load += batteryChillerPowerDraw;
        if (ptHeaterOn) hv_aux_load += ptHeaterPowerDraw;
        if (ptChillerOn) hv_aux_load += ptChillerPowerDraw;
    }

    // If Emergency: strictly lock to 20%. Otherwise: apply Thermal Derate and Drive Mode.
    double modeMultiplier = emergencyShutdown ? 0.20 : (thermalDerate ? 0.70 : 1.0) * driveModeLimit;
    
    // If ignition is off or pre-checks not ready, prevent propulsion
    if (!ignitionOn || precheckStatus != PrecheckStatus::Ready) {
        throttle = 0.0;
        brake = 1.0; // Force brake to ensure no movement
        modeMultiplier = 0.0; // No propulsion power
    }

    // Total available power from battery after mode restrictions and aux loads
    double available_battery_power = (maxPower * modeMultiplier) - hv_aux_load;
    // Net power available for propulsion (accounting for approx 94% motor/inv efficiency)
    double propulsion_limit = std::max(0.0, available_battery_power * 0.94);

    // Slew Rate Limiter: 3.0s for 0.3 power change (100% -> 70%) -> 0.1 units/sec
    double maxStep = 0.1 * dt;
    static double smoothLimit = 1.0; 
    if (smoothLimit < modeMultiplier)
        smoothLimit = std::min(modeMultiplier, smoothLimit + maxStep);
    else if (smoothLimit > modeMultiplier)
        smoothLimit = std::max(modeMultiplier, smoothLimit - maxStep);

    double powerLimit = smoothLimit; // Apply the slew-rate limited power factor

    // 1. Calculate Drive Torque (Constant Torque -> Constant Power)
    double angular_vel = speed / wheelRadius;
    double motor_rpm = (angular_vel * 60.0 / (2.0 * M_PI)) * 9.04;
    
    double active_max_torque = maxWheelTorque + (configuration == PowertrainConfig::AWD ? maxFrontWheelTorque : 0.0);
    double active_max_regen = maxRegenTorque + (configuration == PowertrainConfig::AWD ? maxFrontRegenTorque : 0.0);

    double total_avail_torque = (motor_rpm < 5000) ?
        active_max_torque : std::min(active_max_torque, propulsion_limit / std::max(0.01, speed / wheelRadius));

    // 1a. RPM Safety Limiter (Tesla RDU structural limit at 18,000 RPM)
    if (motor_rpm > 17800.0) {
        double rpm_limit_factor = std::max(0.0, (18000.0 - motor_rpm) / 200.0);
        total_avail_torque *= rpm_limit_factor;
    }

    // Torque Vectoring: Rear handles heavy lifting, Front assists at high demand
    double target_engine_torque = throttle * powerLimit * total_avail_torque;
    double rear_engine_torque = std::min(target_engine_torque, maxWheelTorque);
    double front_engine_torque = std::max(0.0, target_engine_torque - rear_engine_torque);
    if (configuration == PowertrainConfig::RWD) front_engine_torque = 0.0;

    double engine_force = (rear_engine_torque + front_engine_torque) / wheelRadius;

    // 2. Resistance Forces
    // Relative velocity determines drag: +windSpeed is a tailwind (reduces relative velocity)
    double v_rel = speed - windSpeed;
    double drag_force = 0.5 * (baseDragCoeff * currentDragCoeffMultiplier) * FRONTAL_AREA * airDensity * v_rel * std::abs(v_rel);

    double rolling_force = currentRollingResistCoeff * mass * 9.81;
    double gradient_force = mass * 9.81 * std::sin(gradient * M_PI / 180.0);

    // 3. Braking Logic
    double target_regen_torque = std::min(brake, 0.5) / 0.5 * active_max_regen;
    double front_regen_torque = 0.0;
    double rear_regen_torque = 0.0;

    if (configuration == PowertrainConfig::AWD) {
        // Regen Vectoring: Front motor takes priority for recuperation to maintain dynamic stability
        front_regen_torque = std::min(target_regen_torque, maxFrontRegenTorque);
        rear_regen_torque = std::max(0.0, target_regen_torque - front_regen_torque);
    } else {
        rear_regen_torque = target_regen_torque;
    }

    double regen_force = (rear_regen_torque + front_regen_torque) / wheelRadius;
    double mech_brake_force = std::max(0.0, brake - 0.5) / 0.5 * 8.0 * mass;

    double net_force = engine_force - drag_force - rolling_force - regen_force - mech_brake_force - gradient_force;
    
    // 4. Physics Integration
    double acceleration = net_force / mass;
    bool wasMoving = speed > 0.01; // 0.01 m/s threshold
    speed = std::max(0.0, speed + acceleration * dt);
    distance += speed * dt;

    // Reset brake to 0.0 if we just came to a full stop while driving
    if (wasMoving && speed <= 0.0 && ignitionOn && precheckStatus == PrecheckStatus::Ready) {
        brake = 0.0;
    }

    // 5. Energy Consumption
    double rear_mech_power = rear_engine_torque * (speed / wheelRadius);
    double front_mech_power = front_engine_torque * (speed / wheelRadius);
    double mech_power = rear_mech_power + front_mech_power;
    double speed_kmh = speed * 3.6;    

    // Simple Voltage-Drop Model based on SOC
    double soc = batteryEnergy / batteryCapacity;
    // Voltage ranges from ~420V (Full) to ~320V (Empty)
    double current_voltage = 320.0 + (100.0 * soc);
    
    // Detailed Propulsion Efficiency (from Master Spec)
    double motor_rpm_norm = std::min(motor_rpm / 18000.0, 1.0);
    double motor_eff = std::max(0.70, MOTOR_ETA_BASE - (MOTOR_K_COPPER * std::pow(throttle, 2)) - (MOTOR_K_IRON * motor_rpm_norm));
    double inv_eff = std::max(0.85, INV_ETA_BASE - (INV_K_SWITCH * throttle));

    double front_motor_eff = motor_eff; // Simulating similar induction/PMSM curve
    double front_inv_eff = inv_eff;

    // Total electrical draw: mech_power corrected by inverter/motor efficiencies + cabin aux
    double electrical_draw = (rear_mech_power / (motor_eff * inv_eff)) + hv_aux_load;
    if (configuration == PowertrainConfig::AWD && front_mech_power > 0) {
        electrical_draw += (front_mech_power / (front_motor_eff * front_inv_eff));
    }

    // Battery Heat based on current (I = P/V)
    double battery_current = electrical_draw / current_voltage;
    // Joule Heating: Q = I^2 * R. Internal Resistance roughly 0.042 Ohms.
    double battery_heat = std::pow(battery_current, 2) * 0.042;

    double rear_regen_power = (rear_regen_torque / wheelRadius) * speed;
    double front_regen_power = (front_regen_torque / wheelRadius) * speed;
    double regen_power = rear_regen_power + front_regen_power;
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
    double motor_heat = (std::abs(rear_mech_power) + std::abs(rear_regen_power)) * (1.0 - motor_eff);
    double inv_heat = (std::abs(rear_mech_power) + std::abs(rear_regen_power)) * (1.0 - inv_eff);
    
    double front_motor_heat = 0.0;
    double front_inv_heat = 0.0;
    if (configuration == PowertrainConfig::AWD) {
        front_motor_heat = (std::abs(front_mech_power) + std::abs(front_regen_power)) * (1.0 - front_motor_eff);
        front_inv_heat = (std::abs(front_mech_power) + std::abs(front_regen_power)) * (1.0 - front_inv_eff);
    }

    auto calculateDissipation = [&](double temp, double refCoolantTemp, CoolingAction action, double& heatToCoolant) {
        double q_air_base = NATURAL_CONVECTION * (temp - ambientTemp);
        heatToCoolant = 0.0;

        if (action != CoolingAction::TURNED_OFF) {
            double h_liq = 0.0;
            switch(action) {
                case CoolingAction::LIQUID_WARM: 
                    h_liq = LIQUID_COOLING_WARM; 
                    // Logic: Fans remain OFF, only pump runs to distribute heat
                    break;
                case CoolingAction::LIQUID_LOW:  h_liq = LIQUID_COOLING_LOW; break;
                case CoolingAction::LIQUID_MED:  h_liq = LIQUID_COOLING_MED; break;
                case CoolingAction::LIQUID_HIGH: h_liq = LIQUID_COOLING_HIGH; break;
                default: break;
            }
            double q_liq = h_liq * (temp - refCoolantTemp);
            heatToCoolant = q_liq; // Allow bidirectional heat transfer to warm components
            return q_air_base + q_liq;
        } else {
            return q_air_base;
        }
    };

    double mToPT, iToPT, bToBat;
    double fMToPT = 0.0, fIToPT = 0.0;
    double mDiss = calculateDissipation(motorTemp, coolantPTTemp, motorAction, mToPT);
    double iDiss = calculateDissipation(inverterTemp, coolantPTTemp, inverterAction, iToPT);
    double bDiss = calculateDissipation(batteryTemp, coolantBatTemp, batteryAction, bToBat);

    if (configuration == PowertrainConfig::AWD) {
        double fmDiss = calculateDissipation(frontMotorTemp, coolantPTTemp, frontMotorAction, fMToPT);
        double fiDiss = calculateDissipation(frontInverterTemp, coolantPTTemp, frontInverterAction, fIToPT);
        frontMotorTemp += ((front_motor_heat - fmDiss) * dt) / frontMotorThermalMass;
        frontInverterTemp += ((front_inv_heat - fiDiss) * dt) / frontInverterThermalMass;
    } else {
        double fmDiss = calculateDissipation(frontMotorTemp, ambientTemp, CoolingAction::TURNED_OFF, fMToPT);
        double fiDiss = calculateDissipation(frontInverterTemp, ambientTemp, CoolingAction::TURNED_OFF, fIToPT);
        frontMotorTemp += (-fmDiss * dt) / frontMotorThermalMass;
        frontInverterTemp += (-fiDiss * dt) / frontInverterThermalMass;
    }

    motorTemp += ((motor_heat - mDiss) * dt) / motorThermalMass;
    inverterTemp += ((inv_heat - iDiss) * dt) / inverterThermalMass;
    batteryTemp += ((battery_heat - bDiss) * dt) / batteryThermalMass;

    // 7. Coolant Loop Physics (Radiator + Ram Air)
    // Radiator cooling depends on the magnitude of relative air velocity
    double h_ram_boost = RAM_AIR_K * v_rel * v_rel;

    // Dynamic Fan Rejection (Hardware Logic: PT=2 fans, BAT=1 fan)
    // PT Loop: Escalate from 1 fan to 2 fans based on demand
    double pt_fan_boost = 0.0;
    if (motorAction == CoolingAction::LIQUID_HIGH || inverterAction == CoolingAction::LIQUID_HIGH || frontMotorAction == CoolingAction::LIQUID_HIGH || frontInverterAction == CoolingAction::LIQUID_HIGH)
        pt_fan_boost = 120.0; // Both fans at max
    else if (motorAction >= CoolingAction::LIQUID_LOW || inverterAction >= CoolingAction::LIQUID_LOW || frontMotorAction >= CoolingAction::LIQUID_LOW || frontInverterAction >= CoolingAction::LIQUID_LOW)
        pt_fan_boost = 50.0;  // Single fan or low-duty cycle

    // Bat Loop: Single auxiliary fan
    double bat_fan_boost = (batteryAction >= CoolingAction::LIQUID_LOW) ? 60.0 : 0.0;
    
    // Powertrain Loop (Motor + Inverter + Front unit if AWD)
    double pt_rad_diss = (COOLANT_PT_RAD_H + pt_fan_boost + h_ram_boost) * (coolantPTTemp - ambientTemp);
    coolantPTTemp += ((mToPT + iToPT + fMToPT + fIToPT - pt_rad_diss) * dt) / COOLANT_PT_MASS;

    // Battery Loop
    double heat_from_battery_heater = batteryHeaterOn ? batteryHeaterPowerDraw : 0.0; // Heater directly adds heat to coolant
    double heat_from_battery_chiller = batteryChillerOn ? -batteryChillerPowerDraw : 0.0; // Chiller removes heat
    double bat_rad_diss = (COOLANT_BAT_RAD_H + bat_fan_boost + h_ram_boost) * (coolantBatTemp - ambientTemp); // Radiator dissipates heat
    coolantBatTemp += ((bToBat + heat_from_battery_heater + heat_from_battery_chiller - bat_rad_diss) * dt) / COOLANT_BAT_MASS;

    // Powertrain Loop (Heater/Chiller)
    double heat_from_pt_heater = ptHeaterOn ? ptHeaterPowerDraw : 0.0;
    double heat_from_pt_chiller = ptChillerOn ? -ptChillerPowerDraw : 0.0;
    coolantPTTemp += ((heat_from_pt_heater + heat_from_pt_chiller) * dt) / COOLANT_PT_MASS;

    // 8. Cooling System State Machine (Polled every 5s)
    // Only run pre-checks and thermal management if Ignition is requested
    if (ignitionOn) {
        updatePrecheckLogic(); 
        updateBatteryThermalManagement();
        updatePTThermalManagement();
    }

    coolingPollTimer += dt;
    if (coolingPollTimer >= 5.0) {
        coolingPollTimer = 0.0;
        updateDeviceCooling(motorTemp, MOTOR_OPTIMAL_MIN, MOTOR_OPTIMAL_MAX, MOTOR_NORMAL_MAX, motorAction);
        updateDeviceCooling(inverterTemp, INV_OPTIMAL_MIN, INV_OPTIMAL_MAX, INV_NORMAL_MAX, inverterAction);
        updateDeviceCooling(batteryTemp, BAT_OPTIMAL_MIN, BAT_OPTIMAL_MAX, BAT_NORMAL_MAX, batteryAction);

        if (configuration == PowertrainConfig::AWD) {
            updateDeviceCooling(frontMotorTemp, MOTOR_OPTIMAL_MIN, MOTOR_OPTIMAL_MAX, MOTOR_NORMAL_MAX, frontMotorAction);
            updateDeviceCooling(frontInverterTemp, INV_OPTIMAL_MIN, INV_OPTIMAL_MAX, INV_NORMAL_MAX, frontInverterAction);
        } else {
            frontMotorAction = CoolingAction::TURNED_OFF;
            frontInverterAction = CoolingAction::TURNED_OFF;
        }

        // Emergency if too hot OR too cold
        bool wantsEmergency = (motorTemp > MOTOR_CRITICAL || motorTemp < MOTOR_COLD_LIMIT || // Motor too hot or too cold
                               inverterTemp > INV_CRITICAL || inverterTemp < INV_COLD_LIMIT || // Inverter too hot or too cold
                               batteryTemp > BAT_CRITICAL || batteryTemp < BAT_COLD_LIMIT);   // Battery too hot or too cold
        
        if (configuration == PowertrainConfig::AWD) {
            wantsEmergency |= (frontMotorTemp > MOTOR_CRITICAL || frontMotorTemp < MOTOR_COLD_LIMIT || 
                               frontInverterTemp > INV_CRITICAL || frontInverterTemp < INV_COLD_LIMIT);
        }

        if (wantsEmergency && !emergencyShutdown) { // Only log on transition to emergency
            if (motorTemp > MOTOR_CRITICAL) spdlog::critical("Emergency Shutdown: Motor Overheat ({} C)", motorTemp);
            if (inverterTemp > INV_CRITICAL) spdlog::critical("Emergency Shutdown: Inverter Overheat ({} C)", inverterTemp);
            if (batteryTemp > BAT_CRITICAL) spdlog::critical("Emergency Shutdown: Battery Overheat ({} C)", batteryTemp);
            if (frontMotorTemp > MOTOR_CRITICAL) spdlog::critical("Emergency Shutdown: Front Motor Overheat ({} C)", frontMotorTemp);
            if (frontInverterTemp > INV_CRITICAL) spdlog::critical("Emergency Shutdown: Front Inverter Overheat ({} C)", frontInverterTemp);
        }

        bool wantsDerate = (motorTemp > MOTOR_NORMAL_MAX || inverterTemp > INV_NORMAL_MAX || batteryTemp > BAT_NORMAL_MAX);
        if (configuration == PowertrainConfig::AWD) {
            wantsDerate |= (frontMotorTemp > MOTOR_NORMAL_MAX || frontInverterTemp > INV_NORMAL_MAX);
        }

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
            if (frontMotorTemp > MOTOR_NORMAL_MAX) derateReason += QString::asprintf("Front Motor (%.1f°C) ", frontMotorTemp);
            if (frontInverterTemp > INV_NORMAL_MAX) derateReason += QString::asprintf("Front Inverter (%.1f°C) ", frontInverterTemp);
            
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

void EVPowertrainSimulator::updateDeviceCooling(double temp, double optimalMin, double optimalMax, double normalMax, CoolingAction &action) {
    if (temp < optimalMin) {
        action = CoolingAction::LIQUID_WARM; // Active heating circulation
    } else if (temp <= optimalMax) {
        // De-escalate cooling toward TURNED_OFF
        if (action == CoolingAction::LIQUID_HIGH)      action = CoolingAction::LIQUID_MED;
        else if (action == CoolingAction::LIQUID_MED)  action = CoolingAction::LIQUID_LOW;
        else if (action == CoolingAction::LIQUID_WARM && temp >= optimalMin + 2.0) action = CoolingAction::TURNED_OFF; // Hysteresis
        else if (action != CoolingAction::LIQUID_WARM) action = CoolingAction::TURNED_OFF;
    } else if (temp <= normalMax) {
        // Maintain LIQUID_LOW if already cooling, otherwise allow passive soak
        if (action > CoolingAction::LIQUID_LOW) {
            action = CoolingAction::LIQUID_LOW;
        }
    } else { // temp > normalMax (Escalate)
        // Step up liquid flow rates to increase heat transfer
        if (action == CoolingAction::TURNED_OFF || action == CoolingAction::LIQUID_WARM) {
            action = CoolingAction::LIQUID_LOW;
        } else if (action == CoolingAction::LIQUID_LOW) {
            action = CoolingAction::LIQUID_MED;
        } else {
            action = CoolingAction::LIQUID_HIGH;
        }
    }
}

void EVPowertrainSimulator::updateBatteryThermalManagement() {
    // Battery Warming
    if (batteryTemp < BAT_OPTIMAL_MIN_HEATER_ON) {
        if (!batteryHeaterOn) { spdlog::info("Battery heater ON: {:.1f}°C", batteryTemp); }
        batteryHeaterOn = true;
    } else if (batteryTemp >= BAT_OPTIMAL_MIN + 2.0) { // Hysteresis for turning off heater
        if (batteryHeaterOn) { spdlog::info("Battery heater OFF: {:.1f}°C", batteryTemp); }
        batteryHeaterOn = false;
    }

    // Battery Cooling (Chiller)
    if (batteryTemp > BAT_OPTIMAL_MAX_CHILLER_ON) {
        if (!batteryChillerOn) { spdlog::info("Battery chiller ON: {:.1f}°C", batteryTemp); }
        batteryChillerOn = true;
    } else if (batteryTemp <= BAT_OPTIMAL_MAX - 2.0) { // Hysteresis for turning off chiller
        if (batteryChillerOn) { spdlog::info("Battery chiller OFF: {:.1f}°C", batteryTemp); }
        batteryChillerOn = false;
    }
}

void EVPowertrainSimulator::updatePTThermalManagement() {
    // PT Warming
    if (coolantPTTemp < PT_OPTIMAL_MIN_HEATER_ON) {
        if (!ptHeaterOn) { spdlog::info("PT heater ON: {:.1f}°C", coolantPTTemp); }
        ptHeaterOn = true;
    } else if (coolantPTTemp >= MOTOR_OPTIMAL_MAX + 2.0) { // Hysteresis for turning off heater
        if (ptHeaterOn) { spdlog::info("PT heater OFF: {:.1f}°C", coolantPTTemp); }
        ptHeaterOn = false;
    }

    // PT Cooling (Chiller)
    if (coolantPTTemp > PT_OPTIMAL_MAX_CHILLER_ON) {
        if (!ptChillerOn) { spdlog::info("PT chiller ON: {:.1f}°C", coolantPTTemp); }
        ptChillerOn = true;
    } else if (coolantPTTemp <= MOTOR_OPTIMAL_MAX - 2.0) { // Hysteresis for turning off chiller
        if (ptChillerOn) { spdlog::info("PT chiller OFF: {:.1f}°C", coolantPTTemp); }
        ptChillerOn = false;
    }
}

void EVPowertrainSimulator::updatePrecheckLogic() {
    // Battery Pre-check
    if (batteryTemp < BAT_OPTIMAL_MIN) {
        precheckStatus = PrecheckStatus::HeatingBattery;
        return;
    }
    if (batteryTemp > BAT_OPTIMAL_MAX) {
        precheckStatus = PrecheckStatus::CoolingBattery;
        return;
    }

    // Powertrain Pre-check (using coolant temp as proxy for overall PT temp)
    // Note: Motor/Inverter optimal max are used here for the PT loop's "ready" state
    if (coolantPTTemp < MOTOR_COLD_LIMIT) { // Only block if below structural limits (-20C)
        precheckStatus = PrecheckStatus::HeatingPT;
        return;
    }
    if (coolantPTTemp > MOTOR_OPTIMAL_MAX + 5.0) { // Allow some buffer for PT to cool down
        precheckStatus = PrecheckStatus::CoolingPT;
        return;
    }

    // If all checks pass
    if (precheckStatus != PrecheckStatus::Ready) {
        spdlog::info("Pre-checks complete. System Ready.");
    }
    precheckStatus = PrecheckStatus::Ready;
}

void EVPowertrainSimulator::setAmbientTemp(double t) {
    ambientTemp = t;
    // Apply equilibrium only if vehicle is at rest AND has not yet started its journey
    if (speed < 0.1 && distance < 0.001) {
        motorTemp = t;
        inverterTemp = t;
            frontMotorTemp = t;
            frontInverterTemp = t;
        batteryTemp = t;
        coolantPTTemp = t;
        coolantBatTemp = t;
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
    // Slew rate limiter will smoothly transition to this new driveModeLimit
}

void EVPowertrainSimulator::setConfiguration(int index) {
    configuration = static_cast<PowertrainConfig>(index);
    spdlog::info("Powertrain Configuration set to: {}", (index == 0 ? "RWD" : "AWD"));
}