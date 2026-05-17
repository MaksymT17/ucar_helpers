import fastf1
import pandas as pd
import fastf1.utils

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
        # 1. Get continuous telemetry for the race laps
        # Using race_laps.get_telemetry() directly provides a joined stream
        telemetry = race_laps.get_telemetry()

        # 2. Map LapNumber using SessionTime to ensure the lap changes exactly at the line
        # We use standard pandas merge_asof to avoid version-specific FastF1 helper errors
        lap_timing = race_laps[['LapNumber', 'LapStartTime']].copy()
        lap_timing = lap_timing.sort_values(by='LapStartTime')
        
        telemetry = pd.merge_asof(
            telemetry.sort_values('SessionTime'),
            lap_timing,
            left_on='SessionTime',
            right_on='LapStartTime',
            direction='backward'
        )
        
        # 3. CLEANUP: Ensure Time is strictly monotonic and remove spatial overlaps
        telemetry = telemetry.drop_duplicates(subset=['SessionTime'], keep='first')
        telemetry['LapNumber'] = telemetry['LapNumber'].ffill().bfill().astype(int)

        # NORMALIZATION: Shift Distance so 0.0 is the Finish Line
        # Find the distance value exactly where Lap 2 starts
        lap2_telemetry = telemetry[telemetry['LapNumber'] == 2]
        if not lap2_telemetry.empty:
            finish_line_offset = lap2_telemetry['Distance'].min()
            telemetry['Distance'] = telemetry['Distance'] - finish_line_offset

        # Normalize Time: Subtract the start time of the first race lap
        start_time = telemetry['Time'].iloc[0]
        telemetry['TimeSeconds'] = (telemetry['Time'] - start_time).dt.total_seconds()

        # Export to a separate CSV file named by year, GP, and driver abbreviation
        filename = f'australia_2026_{abb}_telemetry.csv'
        telemetry[['TimeSeconds', 'X', 'Y', 'Speed', 'Distance', 'Throttle', 'Brake', 'LapNumber']].to_csv(filename, index=True)
        print(f"Exported telemetry for {abb} to {filename}")