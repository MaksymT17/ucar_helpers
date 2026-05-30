#ifndef POWERTRAIN_SIMULATOR_H
#define POWERTRAIN_SIMULATOR_H

#include <cmath>
#include "vehicle_dto.h"

class EVPowertrainSimulator {
public:
    EVPowertrainSimulator();

    void update(double dt);

    // Controls
    void setThrottle(double t) { throttle = t; }
    void setBrake(double b) { brake = b; }
    void setGradient(double g) { gradient = g; }
    void setAmbientTemp(double t);
    void setWindSpeed(double w) { windSpeed = w; }
    void setDriveMode(int index);
    void setConfiguration(int index);
    void setIgnition(bool on);

    // State Getters
    double getSpeedKmh() const { return speed * 3.6; }
    double getBatteryKwh() const { return batteryEnergy / 3600000.0; }
    double getDistanceKm() const { return distance / 1000.0; }
    double getTripTime() const { return tripTime; }
    double getPowerKw() const { return lastPowerKw; }
    double getEfficiency() const { return lastEfficiencyKwh100; }
    double getSOC() const { return (batteryEnergy / batteryCapacity) * 100.0; }
    double getMotorTemp() const { return motorTemp; }
    double getInverterTemp() const { return inverterTemp; }
    double getFrontMotorTemp() const { return frontMotorTemp; }
    double getFrontInverterTemp() const { return frontInverterTemp; }
    CoolingAction getFrontMotorAction() const { return frontMotorAction; }
    CoolingAction getFrontInverterAction() const { return frontInverterAction; }
    double getBatteryTemp() const { return batteryTemp; }
    double getCoolantPTTemp() const { return coolantPTTemp; }
    double getCoolantBatTemp() const { return coolantBatTemp; }
    double getAmbientTemp() const { return ambientTemp; }
    double getWindSpeed() const { return windSpeed; }
    bool isEmergency() const { return emergencyShutdown; }
    bool isDerated() const { return thermalDerate; }
    DriveMode getDriveMode() const { return driveMode; }
    PowertrainConfig getConfiguration() const { return configuration; }
    bool isIgnitionOn() const { return ignitionOn; }
    double getThrottle() const { return throttle; }
    double getBrake() const { return brake; }
    double get12VPercentage() const { return (battery12VEnergy / battery12VCapacity) * 100.0; }
    PrecheckStatus getPrecheckStatus() const { return precheckStatus; }

    void setSurfaceType(int typeIndex);

    // Auxiliary Systems
    void setLowBeams(bool on) { lowBeamsOn = on; }
    void setHighBeams(bool on) { highBeamsOn = on; }
    void setInfotainmentPower(double watts) { infotainmentPowerDraw = watts; }
    void setACOn(bool on) { acOn = on; }
    void setACTargetTemp(double t) { acTargetTemp = t; }

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

    // Thermal Thresholds (Synced with Python Master Spec)
    static constexpr double MOTOR_OPTIMAL_MAX = 80.0;
    static constexpr double MOTOR_OPTIMAL_MIN = 40.0;
    static constexpr double MOTOR_NORMAL_MAX  = 90.0;
    static constexpr double MOTOR_CRITICAL    = 140.0;

    static constexpr double INV_OPTIMAL_MAX   = 65.0;
    static constexpr double INV_OPTIMAL_MIN   = 35.0;
    static constexpr double INV_NORMAL_MAX    = 75.0;
    static constexpr double INV_CRITICAL      = 120.0;

    static constexpr double BAT_OPTIMAL_MAX   = 35.0;
    static constexpr double BAT_OPTIMAL_MIN   = 20.0; // Optimal minimum for battery temperature
    static constexpr double BAT_NORMAL_MAX    = 45.0;
    static constexpr double BAT_CRITICAL      = 50.0;

    static constexpr double MOTOR_COLD_LIMIT  = -20.0;
    static constexpr double INV_COLD_LIMIT    = -20.0;
    static constexpr double BAT_COLD_LIMIT    = -10.0;

    // Thermal Management Thresholds for Heater/Chiller Activation (with hysteresis)
    static constexpr double BAT_OPTIMAL_MIN_HEATER_ON = 18.0;
    static constexpr double BAT_OPTIMAL_MAX_CHILLER_ON = 37.0;
    static constexpr double PT_OPTIMAL_MIN_HEATER_ON = 38.0; // Using a lower bound for PT coolant
    static constexpr double PT_OPTIMAL_MAX_CHILLER_ON = 82.0; // Using an upper bound for PT coolant

    // Thermal Constants
    static constexpr double NATURAL_CONVECTION    = 5.0;
    static constexpr double LIQUID_COOLING_LOW     = 120.0;
    static constexpr double LIQUID_COOLING_MED     = 250.0;
    static constexpr double LIQUID_COOLING_HIGH    = 450.0;
    static constexpr double LIQUID_COOLING_WARM    = 60.0;
    static constexpr double RAM_AIR_K             = 0.06;
    static constexpr double COOLANT_PT_RAD_H      = 80.0;
    static constexpr double COOLANT_BAT_RAD_H     = 120.0;
    static constexpr double COOLANT_PT_MASS       = 33700.0;
    static constexpr double COOLANT_BAT_MASS      = 22400.0;

    // Environment & Physics
    static constexpr double AIR_PRESSURE_SEA_LEVEL = 101325.0;
    static constexpr double SPECIFIC_GAS_CONST_AIR = 287.05;
    static constexpr double FRONTAL_AREA           = 2.2;

    // Efficiency Coefficients (Tesla RDU Baseline)
    static constexpr double MOTOR_ETA_BASE = 0.95;
    static constexpr double MOTOR_K_COPPER = 0.08;
    static constexpr double MOTOR_K_IRON   = 0.04;
    static constexpr double INV_ETA_BASE   = 0.98;
    static constexpr double INV_K_SWITCH   = 0.03;

    double lastEfficiencyKwh100 = 0.0;
    double effPowerSum = 0.0;
    double effSpeedSum = 0.0;
    int effTicks = 0;

    double currentRollingResistCoeff;
    double currentDragCoeffMultiplier;

    bool emergencyShutdown = false;
    bool thermalDerate = false;
    double smoothPowerLimit = 1.0;

    // Drive Mode & Hysteresis
    DriveMode driveMode = DriveMode::SPORT;
    double driveModeLimit = 1.0;
    double modeSwitchTimer = 30.0; // Ready for first change

    // Powertrain Configuration
    PowertrainConfig configuration = PowertrainConfig::RWD;

    // Thermal State
    double windSpeed = 0.0;     // m/s (+ tailwind, - headwind)
    double ambientTemp = 25.0;
    double motorTemp = 25.0;
    double inverterTemp = 25.0;
    double frontMotorTemp = 25.0;
    double frontInverterTemp = 25.0;
    double batteryTemp = 25.0;
    double coolantPTTemp = 25.0;
    double coolantBatTemp = 25.0;

    CoolingAction motorAction = CoolingAction::TURNED_OFF;
    CoolingAction inverterAction = CoolingAction::TURNED_OFF;
    CoolingAction frontMotorAction = CoolingAction::TURNED_OFF;
    CoolingAction frontInverterAction = CoolingAction::TURNED_OFF;
    CoolingAction batteryAction = CoolingAction::TURNED_OFF;
    double coolingPollTimer = 0.0;
    PrecheckStatus precheckStatus = PrecheckStatus::Initializing;

    // Auxiliary Loads
    bool lowBeamsOn = false;
    bool highBeamsOn = false;
    bool ignitionOn = false;
    bool dcdcActive = false;
    bool batteryHeaterOn = false;
    double batteryHeaterPowerDraw = 3000.0; // Example: 3kW heater
    bool batteryChillerOn = false;
    double batteryChillerPowerDraw = 1000.0; // Example: 1kW chiller
    bool ptHeaterOn = false;
    double ptHeaterPowerDraw = 2000.0; // Example: 2kW heater for PT loop
    bool ptChillerOn = false;
    double ptChillerPowerDraw = 1500.0; // Example: 1.5kW chiller for PT loop
    double infotainmentPowerDraw = 150.0; // Standard compute/screen load
    bool acOn = false;
    double acTargetTemp = 22.0;

    // 12V Auxiliary System
    double battery12VEnergy = 2592000.0; // 60Ah * 12V in Joules
    const double battery12VCapacity = 2592000.0;
    const double DCDC_MAX_POWER = 1500.0; // 1.5kW DC-DC Converter
    static constexpr double SYSTEM_BASE_LOAD = 250.0; // Computers, sensors, etc.

    void updateDeviceCooling(double temp, double optimalMin, double optimalMax, double normalMax, CoolingAction &action);

    // Constants (Synced with Python Master Spec)
    const double mass = 1850.0;
    const double maxWheelTorque = 3600.0;
    const double maxFrontWheelTorque = 1200.0;
    const double maxRegenTorque = 1800.0;
    const double maxFrontRegenTorque = 1200.0;
    const double maxPower = 210000.0;
    const double wheelRadius = 0.33;
    const double batteryCapacity = 75.0 * 3600000.0;
    const double nominalVoltage = 400.0; // Nominal pack voltage
    const double baseDragCoeff = 0.25; // Base drag coefficient

    // Surface Coefficients
    const double ASPHALT_ROLLING_RESIST_COEFF = 0.012;
    const double ASPHALT_DRAG_COEFF_MULTIPLIER = 1.0;
    const double GRAVEL_ROLLING_RESIST_COEFF = 0.025;
    const double GRAVEL_DRAG_COEFF_MULTIPLIER = 1.05;
    const double ICE_ROLLING_RESIST_COEFF = 0.005;

    // Thermal Constants
    const double motorThermalMass = 22000.0;
    const double batteryThermalMass = 309000.0;
    const double inverterThermalMass = 3400.0;
    const double frontMotorThermalMass = 15000.0;
    const double frontInverterThermalMass = 2500.0;

    void updateBatteryThermalManagement();
    void updatePTThermalManagement();
    void updatePrecheckLogic();
};
#endif