import json
import re
from collections import defaultdict

LOG_FILE_PATH = "/Users/mba23/projects/ucar_helpers/timing/build/race_analytics.log"

# Thresholds to filter out telemetry glitches ("illusions" or wrong values)
MAX_LOW_SPD_ACCEL = 100.0  # Max realistic low-speed acceleration (km/h)/s
MAX_HIGH_SPD_ACCEL = 80.0  # Max realistic high-speed acceleration (km/h)/s
MAX_TOP_SPEED = 370.0      # Max realistic F1 top speed in km/h

def main():
    # Dictionary to store the highest valid values for each driver
    driver_stats = defaultdict(lambda: {
        "max_low_spd_accel": 0.0,
        "max_high_spd_accel": 0.0,
        "max_top_speed": 0.0
    })

    # Regex to extract the JSON payload from the spdlog formatted line
    json_pattern = re.compile(r'(\{.*\})')

    try:
        with open(LOG_FILE_PATH, 'r') as file:
            for line in file:
                match = json_pattern.search(line)
                if not match:
                    continue
                
                try:
                    data = json.loads(match.group(1))
                except json.JSONDecodeError:
                    continue
                
                driver = data.get("drv")
                if not driver:
                    continue

                # Extract powertrain properties
                trq_low_spd = data.get("trq", 0.0)
                pwr_high_spd = data.get("pwr", 0.0)
                top_spd = data.get("drg", 0.0)

                # Update driver stats, actively ignoring "illusion" spikes
                stats = driver_stats[driver]
                
                if 0.0 < trq_low_spd < MAX_LOW_SPD_ACCEL:
                    stats["max_low_spd_accel"] = max(stats["max_low_spd_accel"], trq_low_spd)
                    
                if 0.0 < pwr_high_spd < MAX_HIGH_SPD_ACCEL:
                    stats["max_high_spd_accel"] = max(stats["max_high_spd_accel"], pwr_high_spd)
                    
                if 0.0 < top_spd < MAX_TOP_SPEED:
                    stats["max_top_speed"] = max(stats["max_top_speed"], top_spd)

    except FileNotFoundError:
        print(f"Error: Log file not found at {LOG_FILE_PATH}")
        return

    # Filter out drivers that only have safety car/pit lane data (never exceeded racing speeds)
    valid_drivers = {d: s for d, s in driver_stats.items() if s["max_top_speed"] >= 250.0}

    metrics = [
        ("Traction (Low Spd Accel)", "max_low_spd_accel", "(km/h)/s"),
        ("Peak Power (High Spd Accel)", "max_high_spd_accel", "(km/h)/s"),
        ("Drag (Top Speed)", "max_top_speed", "km/h")
    ]

    print("=== Powertrain Analytics (Filtered & Sorted) ===")
    
    for title, key, unit in metrics:
        print(f"\n--- Top {title} ---")
        print(f"{'Driver':<8} | {'Value':<10} | {'Unit'}")
        print("-" * 35)
        
        # Sort each category individually, highest values first
        sorted_drivers = sorted(valid_drivers.items(), key=lambda x: x[1][key], reverse=True)
        
        for driver, stats in sorted_drivers:
            print(f"{driver:<8} | {stats[key]:<10.2f} | {unit}")

if __name__ == '__main__':
    main()