#ifndef POWERTRAIN_SIMULATOR_H
#define POWERTRAIN_SIMULATOR_H

#include <cmath>

enum class CoolingAction {
    LIQUID_WARM = -1,
    NONE = 0,
    PASSIVE = 1,
    ACTIVE_FAN = 2,
    LIQUID_COLD = 3
};

class EVPowertrainSimulator {
public:
    EVPowertrainSimulator();

    void update(double dt);

    // Controls
    void setThrottle(double t) { throttle = t; }
    void setBrake(double b) { brake = b; }
    void setGradient(double g) { gradient = g; }
    void setAmbientTemp(double t) { ambientTemp = t; }

    // State Getters
    double getSpeedKmh() const { return speed * 3.6; }
    double getBatteryKwh() const { return batteryEnergy / 3600000.0; }
    double getDistanceKm() const { return distance / 1000.0; }
    double getTripTime() const { return tripTime; }
    double getPowerKw() const { return lastPowerKw; }
    double getMotorTemp() const { return motorTemp; }
    double getInverterTemp() const { return inverterTemp; }
    double getBatteryTemp() const { return batteryTemp; }
    double getCoolantPTTemp() const { return coolantPTTemp; }
    double getCoolantBatTemp() const { return coolantBatTemp; }
    double getAmbientTemp() const { return ambientTemp; }
    bool isEmergency() const { return emergencyShutdown; }
    bool isDerated() const { return thermalDerate; }

    void setSurfaceType(int typeIndex);

    CoolingAction getMotorAction() const { return motorAction; }
    CoolingAction getInverterAction() const { return inverterAction; }
    CoolingAction getBatteryAction() const { return batteryAction; }

private:
    double speed = 0.0;         // m/s
    double batteryEnergy;       // Joules
    double throttle = 0.0;      // 0.0 to 1.0
    double brake = 0.0;         // 0.0 to 1.0
    double gradient = 0.0;      // degrees
    double distance = 0.0;      // meters
    double tripTime = 0.0;      // seconds
    double lastPowerKw = 0.0;

    double currentRollingResistCoeff;
    double currentDragCoeffMultiplier;

    bool emergencyShutdown = false;
    bool thermalDerate = false;

    // Thermal State
    double ambientTemp = 25.0;
    double motorTemp = 25.0;
    double inverterTemp = 25.0;
    double batteryTemp = 25.0;
    double coolantPTTemp = 25.0;
    double coolantBatTemp = 25.0;

    CoolingAction motorAction = CoolingAction::NONE;
    CoolingAction inverterAction = CoolingAction::NONE;
    CoolingAction batteryAction = CoolingAction::NONE;
    double coolingPollTimer = 0.0;

    void updateDeviceCooling(double temp, double optimalMax, double normalMax, CoolingAction &action);

    // Constants (Synced with Python Master Spec)
    const double mass = 1850.0;
    const double maxWheelTorque = 3600.0;
    const double maxPower = 210000.0;
    const double wheelRadius = 0.33;
    const double batteryCapacity = 75.0 * 3600000.0;
    const double baseDragCoeff = 0.23; // Base drag coefficient

    // Surface Coefficients
    const double ASPHALT_ROLLING_RESIST_COEFF = 0.0120;
    const double ASPHALT_DRAG_COEFF_MULTIPLIER = 1.0;
    const double GRAVEL_ROLLING_RESIST_COEFF = 0.025;
    const double GRAVEL_DRAG_COEFF_MULTIPLIER = 1.05;
    const double ICE_ROLLING_RESIST_COEFF = 0.005;

    // Thermal Constants
    const double motorThermalMass = 22000.0;
    const double batteryThermalMass = 309000.0;
    const double inverterThermalMass = 3400.0;
};

#endif