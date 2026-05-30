# Executive Master Specification & Engineering Blueprint: Alpha EV Prototype
**Version:** 2.0 (Includes Asymmetric AWD Modality & Dynamic Vectoring)
**Classification:** Core Architectural Context

---

## 1. Core Architectural Layout & Structural Topology
*   **Body Topology:** Technical Notchback Sedan (3-box configuration). 
*   **Structural Integrity:** Features fixed rear glass with an isolated deck-lid (trunk). This geometric isolation maximises torsional rigidity (target: >35,000 Nm/deg) to robustly manage the high instantaneous torque delivered by the electrified propulsion system.
*   **Dimensional Constraints:**
    *   *Wheelbase:* 2,800 mm (Calculated to optimise high-speed yaw stability whilst maximising volumetric efficiency for the energy storage system).
    *   *Ground Clearance:* 170 mm (Calibrated for mixed-surface reliability and underbody aero-stewardship).

## 2. Propulsion Architectures & Variant Specifications
The platform utilises a modular, software-defined propulsion strategy. To maintain strict configuration management, all variant-specific parameters are consolidated below.

### 2.1. Variant Comparison Matrix
| Specification | Alpha RWD (Baseline) | Alpha AWD (Performance) |
| :--- | :--- | :--- |
| **Kerb Mass** | 1,850 kg | 1,930 kg |
| **Weight Distribution** | 48/52 (Front/Rear bias) | 50/50 (Front/Rear bias) |
| **Primary Propulsion (Rear)** | Tesla M3 RDU (PMSM) | Tesla M3 RDU (PMSM) |
| **Secondary Propulsion (Front)**| N/A | AC Induction Motor (ACIM) |
| **Peak System Power** | ~210 kW | ~270 kW (210 kW + 60 kW) |
| **Peak Wheel Torque** | ~3,600 Nm | ~4,800 Nm (3,600 Nm + 1,200 Nm)|
| **Max Regen Torque** | ~1,800 Nm | ~3,000 Nm |

### 2.2. Baseline Rear-Wheel Drive (RWD) Modality
*   **Drive Unit:** Refurbished Tesla Model 3 Rear Drive Unit (RDU).
*   **Motor Topology:** Permanent Magnet Synchronous Motor (PMSM). Highly efficient for primary propulsion.
*   **Peak Output:** ~210 kW (Software-limited) / ~450 Nm nominal (Yielding ~3,600 Nm maximum wheel torque after gear reduction).
*   **Drivetrain:** Single-speed reduction gear integrated with a mechanical open differential.

### 2.3. Asymmetric All-Wheel Drive (AWD) Modality
Introduces a secondary, front-mounted induction motor specifically selected for its distinct electromagnetic characteristics.
*   **Front Drive Unit:** AC Induction Motor (ACIM). 
*   **Asymmetric Advantage:** Exploits the ACIM's lack of permanent magnets to ensure strictly *zero magnetic cogging (drag)* when unpowered. This allows the highly efficient rear PMSM to handle highway cruising whilst the front motor "freewheels" without parasitic losses.
*   **Supplementary Output:** Delivers an additional 20–30% relative capacity (~60 kW peak / ~1,200 Nm wheel torque), yielding a combined system maximum of ~270 kW and 4,800 Nm.
*   **Recuperation Priority Logic:** The front ACIM is designated as the primary kinetic recuperation device. By shifting regenerative braking bias to the front axle, the system mimics traditional hydraulic brake bias, maintaining superior vehicle pitch stability and doubling the maximum potential energy recovery (up to 3,000 Nm combined negative torque).

## 3. Energy Storage & High-Voltage (HV) Systems
*   **Topology:** 75 kWh "Skateboard" Battery Pack (e.g., 96s46p configuration utilising 2170/4680 cylindrical cells).
*   **Geometry:** Flat-pack integration positioned strictly between the axles, significantly lowering the centre of gravity.
*   **Mass:** ~480 kg.
*   **Battery Management (BMS):** Orion BMS 2 (or equivalent Tier-1 equivalent). Responsible for continual state-of-health (SoH) monitoring, cell balancing, and establishing strict thermal operating windows.
*   **Recharging interface:** CCS2 Standard (Ensuring full compatibility with the European/Ukrainian rapid-charging infrastructure).

## 4. Thermodynamics & Active Thermal Management
The vehicle rejects excess entropy via a highly sophisticated, dual-loop liquid cooling architecture.
*   **Loop 1 (Powertrain):** Circulates glycol mixture through the Motor(s) and Inverter(s). Capable of shedding substantial heat loads via ram-air and active dual-fan matrices.
*   **Loop 2 (Battery):** A strictly independent loop designed to maintain the energy storage system within its narrow optimal window (20°C – 35°C), aided by a dedicated HV Chiller circuit.
*   **Cold-Soak Recovery:** Incorporates an active 5 kW HV Positive Temperature Coefficient (PTC) heating matrix. Upon initialisation in sub-zero environments, the system restricts propulsion and actively circulates "Liquid Warm" coolant to elevate critical components above structural cold limits (-20°C) prior to drive enablement.

## 5. Digital Nervous System & Control Logic
*   **Vehicle Control Unit (VCU):** AEM VCU300 (or OpenInverter Logic Board). Replaces proprietary OEM firmware to execute deterministic, real-time CAN-bus translation and inverter modulation.
*   **Supervisory ECU:** Renesas RH850 / R-Car environment.
    *   Executes high-level physics simulations and environmental maps (e.g., thermal derating algorithms).
    *   Drives the minimalist, driver-focused human-machine interface (HMI).
*   **Control API:**
    *   *Propulsion:* Throttle request mapped directly to positive torque (Nm) alongside a slew-rate limiter.
    *   *Recuperation:* Brake request scaled to negative torque (Nm) with zero-speed virtual slip control (Hold mode).

## 6. Vehicle Dynamics & Software-Defined Handling
*   **Friction Brakes:** Purely hydraulic foundation system for the Alpha Prototype (provisioned for future ABS integration).
*   **Virtual Slip Control:** Manages aggressive deceleration exclusively through the VCU, dynamically adjusting negative torque vectors between the front ACIM and rear PMSM to prevent lock-up.
*   **Tuning Methodology:** Relies on "Software-Defined Handling." Dynamics are tuned virtually in C++ physics engines prior to field-flashing on closed testing circuits.

## 7. Production Philosophy & Supply Chain Stewardship
*   **Scale:** Intended as a highly exclusive "Founder’s Series" comprising 15–20 units.
*   **Regulatory Pathway:** Engineered towards Small Series / Individual Vehicle Approval (IVA) standards.
*   **Ethical Sourcing:** Demonstrates a strict commitment to supply chain stewardship, opting for the adaptive reuse and remanufacturing of high-grade Tier-1 OEM hardware (avoiding environmentally and ethically compromising primary extraction).
*   **Business Model:** Operates under a "Systems Integrator" paradigm—bridging the gap between superlative, mechanically sound Tier-1 salvage hardware and bespoke, world-class software engineering.