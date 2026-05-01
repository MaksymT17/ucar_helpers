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

    // 6. Basic Thermal Modeling (Newton's Law of Cooling baseline)
    // Simplified port of the heat logic
    double motor_heat = std::abs(mech_power) * 0.05; // 5% loss approx
    double inv_heat = std::abs(mech_power) * 0.02;   // 2% loss approx
    double battery_heat = std::pow(electrical_draw / 400.0, 2) * 0.042;

    // Natural Convection baseline (h=5)
    double h_nat = 5.0;
    motorTemp += ((motor_heat - h_nat * (motorTemp - ambientTemp)) * dt) / motorThermalMass;
    inverterTemp += ((inv_heat - h_nat * (inverterTemp - ambientTemp)) * dt) / inverterThermalMass;
    batteryTemp += ((battery_heat - h_nat * (batteryTemp - ambientTemp)) * dt) / batteryThermalMass;
    
    // Coolant loops follow ambient for now until the 4-zone state machine is fully ported
    // But they will react to the ambient slider immediately
    coolantPTTemp += (ambientTemp - coolantPTTemp) * 0.1 * dt;
    coolantBatTemp += (ambientTemp - coolantBatTemp) * 0.1 * dt;

    // 7. Cooling System State Machine (Polled every 5s)
    coolingPollTimer += dt;
    if (coolingPollTimer >= 5.0) {
        coolingPollTimer = 0.0;
        updateDeviceCooling(motorTemp, 80.0, 90.0, motorAction);
        updateDeviceCooling(inverterTemp, 65.0, 75.0, inverterAction);
        updateDeviceCooling(batteryTemp, 35.0, 45.0, batteryAction);

        emergencyShutdown = (motorTemp > 140.0 || inverterTemp > 120.0 || batteryTemp > 50.0);
        if (emergencyShutdown) {
            thermalDerate = false;
        } else {
            thermalDerate = (motorTemp > 90.0 || inverterTemp > 75.0 || batteryTemp > 45.0);
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