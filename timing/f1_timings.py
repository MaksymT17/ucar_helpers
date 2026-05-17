import fastf1
import pandas as pd

# Load the 2026 Australian Grand Prix Race session
session = fastf1.get_session(2026, 'Australia', 'R')
session.load()

# Iterate through all drivers in the session
for driver_number in session.drivers:
    # Get driver info to retrieve their abbreviation (e.g., 'VER', 'HAM')
    driver_info = session.get_driver(driver_number)
    abb = driver_info['Abbreviation']
    
    # Get all laps for the driver
    driver_laps = session.laps.pick_driver(driver_number)
    
    # Filter for Race Laps only (Lap 1 onwards) to ensure we start at the grid
    race_laps = driver_laps[driver_laps['LapNumber'] >= 1]

    if not race_laps.empty:
        # Get telemetry lap by lap to include LapNumber in the resulting data
        all_telemetry = []
        for _, lap in race_laps.iterrows():
            t = lap.get_telemetry()
            t['LapNumber'] = lap['LapNumber']
            all_telemetry.append(t)
        telemetry = pd.concat(all_telemetry)

        # Normalize Time: Subtract the start time of the first race lap
        start_time = telemetry['Time'].iloc[0]
        telemetry['TimeSeconds'] = (telemetry['Time'] - start_time).dt.total_seconds()

        # Export to a separate CSV file named by year, GP, and driver abbreviation
        filename = f'australia_2026_{abb}_telemetry.csv'
        telemetry[['TimeSeconds', 'X', 'Y', 'Speed', 'Distance', 'Throttle', 'Brake', 'LapNumber']].to_csv(filename, index=True)
        print(f"Exported telemetry for {abb} to {filename}")