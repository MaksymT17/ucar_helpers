#ifndef VEHICLE_DTO_H
#define VEHICLE_DTO_H

// --------------------------------------------------------------------------
// SYSTEM ENUMERATIONS
// --------------------------------------------------------------------------
enum class CoolingAction {
    LIQUID_WARM = -1,
    TURNED_OFF = 0,
    LIQUID_LOW = 1,
    LIQUID_MED = 2,
    LIQUID_HIGH = 3
};

enum class PrecheckStatus {
    Initializing = 0,
    HeatingBattery = 1,
    CoolingBattery = 2,
    HeatingPT = 3,
    CoolingPT = 4,
    Ready = 5
};

enum class PowertrainConfig {
    RWD = 0,
    AWD = 1
};

enum class DriveMode {
    ECONOMIC = 0,
    NORMAL = 1,
    SPORT = 2
};

// --------------------------------------------------------------------------
// DATA TRANSFER OBJECTS (DTOs)
// In a microservice architecture, these represent the exact JSON/Protobuf 
// messages passed over the network between isolated services.
// --------------------------------------------------------------------------

// 1. Command Message (From UI/Driver to Services)
struct VehicleCommandDTO {
    double throttle = 0.0;
    double brake = 0.0;
    double gradient = 0.0;
    bool ignitionOn = false;
    PowertrainConfig configuration = PowertrainConfig::AWD;
    DriveMode driveMode = DriveMode::SPORT;
    double ambientTemp = 25.0;
    double windSpeed = 0.0;
    int surfaceType = 0; // 0=Asphalt, 1=Gravel, 2=Ice
    double hvAuxLoadW = 0.0;
    bool isPrecheckReady = false;
};

// 2. Power & Dynamics Telemetry (From Math Service to Orchestrator/UI)
struct PowerTelemetryDTO {
    double speedKmh = 0.0;
    double distanceKm = 0.0;
    double tripTime = 0.0;
    double batteryKwh = 75.0;
    double currentPowerKw = 0.0;
    double efficiencyKwh100 = 0.0;
    
    // Heat generation exported exclusively for the Thermal Service to consume
    double rearMotorHeatW = 0.0;
    double rearInverterHeatW = 0.0;
    double frontMotorHeatW = 0.0;
    double frontInverterHeatW = 0.0;
    double batteryHeatW = 0.0;
};

// 3. Thermal Telemetry (From Thermal Service to Orchestrator/UI)
struct ThermalTelemetryDTO {
    double motorTemp = 25.0, inverterTemp = 25.0;
    double frontMotorTemp = 25.0, frontInverterTemp = 25.0;
    double batteryTemp = 25.0, coolantPTTemp = 25.0, coolantBatTemp = 25.0;
    
    CoolingAction motorAction = CoolingAction::TURNED_OFF;
    CoolingAction inverterAction = CoolingAction::TURNED_OFF;
    CoolingAction frontMotorAction = CoolingAction::TURNED_OFF;
    CoolingAction frontInverterAction = CoolingAction::TURNED_OFF;
    CoolingAction batteryAction = CoolingAction::TURNED_OFF;

    bool emergencyShutdown = false;
    bool thermalDerate = false;
    PrecheckStatus precheckStatus = PrecheckStatus::Initializing;
};

#endif