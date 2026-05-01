#include "powertrain_simulator.h"
#include <algorithm>

EVPowertrainSimulator::EVPowertrainSimulator() {
    batteryEnergy = batteryCapacity;
    currentRollingResistCoeff = ASPHALT_ROLLING_RESIST_COEFF;
    currentDragCoeffMultiplier = ASPHALT_DRAG_COEFF_MULTIPLIER;
}

void EVPowertrainSimulator::update(double dt) {
    tripTime += dt;

    double powerLimit = emergencyShutdown ? 0.0 : (thermalDerate ? 0.70 : 1.0);

    // 1. Calculate Drive Torque (Constant Torque -> Constant Power)
    double angular_vel = speed / wheelRadius;
    double motor_rpm = (angular_vel * 60.0 / (2.0 * M_PI)) * 9.04;
    
    double max_avail_torque = (motor_rpm < 5000) ? 
        maxWheelTorque : std::min(maxWheelTorque, maxPower / std::max(0.01, speed / wheelRadius));
    
    double engine_force = (throttle * powerLimit * max_avail_torque) / wheelRadius;

    // 2. Resistance Forces
    double drag_force = 0.5 * (baseDragCoeff * currentDragCoeffMultiplier) * 2.2 * 1.225 * speed * speed;
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
    double dyn_eff = (speed_kmh < 100) ? 0.94 : std::max(0.80, 0.94 - (speed_kmh - 100) / 1000.0);
    
    double electrical_draw = (mech_power / dyn_eff) + 500.0; // 500W overhead
    
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
    double motor_heat = std::abs(mech_power) * 0.05; // 5% loss approx
    double inv_heat = std::abs(mech_power) * 0.02;   // 2% loss approx
    double battery_heat = std::pow(electrical_draw / 400.0, 2) * 0.042;

    auto calculateDissipation = [&](double temp, double refCoolantTemp, CoolingAction action, double& heatToCoolant) {
        double q_air_base = NATURAL_CONVECTION * (temp - ambientTemp);
        heatToCoolant = 0.0;

        if (action == CoolingAction::LIQUID_COLD || action == CoolingAction::LIQUID_WARM) {
            double h_liq = (action == CoolingAction::LIQUID_COLD) ? LIQUID_COOLING_ACTIVE : LIQUID_COOLING_PASSIVE;
            double q_liq = h_liq * (temp - refCoolantTemp);
            heatToCoolant = std::max(0.0, q_liq); // Only positive transfer heats the coolant loop
            return q_air_base + q_liq;
        } else {
            double h_extra = (action == CoolingAction::PASSIVE) ? PASSIVE_COOLING : 
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
    double h_ram_boost = RAM_AIR_K * speed * speed;
    
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

        emergencyShutdown = (motorTemp > MOTOR_CRITICAL || inverterTemp > INV_CRITICAL || batteryTemp > BAT_CRITICAL);
        if (emergencyShutdown) {
            thermalDerate = false;
        } else {
            thermalDerate = (motorTemp > MOTOR_NORMAL_MAX || inverterTemp > INV_NORMAL_MAX || batteryTemp > BAT_NORMAL_MAX);
        }
    }
}

void EVPowertrainSimulator::updateDeviceCooling(double temp, double optimalMax, double normalMax, CoolingAction &action) {
    if (temp <= optimalMax) {
        // Step down cooling toward NONE
        action = static_cast<CoolingAction>(std::max(0, static_cast<int>(action) - 1));
    } else if (temp <= normalMax) {
        // Maintain Passive cooling in normal range
        action = CoolingAction::PASSIVE;
    } else {
        // Escalate cooling logic step-by-step up to Liquid Cold
        action = static_cast<CoolingAction>(std::min(3, static_cast<int>(action) + 1));
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