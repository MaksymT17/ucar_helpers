#ifndef POWER_SERVICE_H
#define POWER_SERVICE_H

#include "vehicle_dto.h"

// --------------------------------------------------------------------------
// POWER & DYNAMICS MICROSERVICE (Isolated Math Engine)
// --------------------------------------------------------------------------
class PowerDeliveryService {
public:
    PowerDeliveryService() = default;

    // Takes inputs and thermal limits, computes new physical state
    PowerTelemetryDTO calculatePhysics(
        const VehicleCommandDTO& inputs, 
        const ThermalTelemetryDTO& thermals, 
        double dt);
        
private:
    // Internal state isolated from the rest of the application
    PowerTelemetryDTO state;
};

#endif