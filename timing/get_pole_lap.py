import fastf1
import pandas as pd
import argparse
import os

# --- Script to fetch and export the pole position lap telemetry for a given GP ---

def get_pole_lap(year, gp_name):
    """
    Fetches the pole position lap for a given year and GP, and saves its 
    telemetry to a CSV file.
    """
    print(f"Fetching session for {year} {gp_name} GP...")
    
    # Enable caching to speed up subsequent runs
    try:
        fastf1.Cache.enable_cache('fastf1_cache')
    except Exception as e:
        print(f"Could not enable FastF1 cache: {e}")

    # Load the Qualifying session data
    session = fastf1.get_session(year, gp_name, 'Q')
    session.load()

    # 1. Get all laps and find the fastest one (the pole lap)
    fastest_lap = session.laps.pick_fastest()

    if fastest_lap is None or pd.isna(fastest_lap['LapTime']):
        print(f"Could not find a valid fastest lap for {year} {gp_name}.")
        return

    # 2. Get the driver's info
    driver_info = session.get_driver(fastest_lap['DriverNumber'])
    abb = driver_info['Abbreviation']

    print(f"Pole lap found: Set by {abb} with a time of {fastest_lap['LapTime']}.")

    # 3. Get the telemetry data for that single lap
    telemetry = fastest_lap.get_telemetry()
    
    if telemetry.empty:
        print(f"Could not retrieve telemetry for the fastest lap.")
        return

    # 4. Process the data for consistency
    # Ensure Time is strictly monotonic
    telemetry = telemetry.drop_duplicates(subset=['SessionTime'], keep='first')

    # Normalize Time: Convert timedelta to seconds from the start of the lap
    start_time = telemetry['Time'].iloc[0]
    telemetry['TimeSeconds'] = (telemetry['Time'] - start_time).dt.total_seconds()

    # Add a LapNumber column. Since it's a single lap, we'll just use '1'.
    telemetry['LapNumber'] = 1

    # 5. Export to CSV in the 'data/<gp_name>' directory
    output_dir = os.path.join('data', gp_name.lower())
    os.makedirs(output_dir, exist_ok=True)
    
    filename = f"{gp_name.lower()}_{year}_POLE_{abb}_telemetry.csv"
    full_path = os.path.join(output_dir, filename)
    
    # Select and order columns to match the C++ parser
    output_columns = ['TimeSeconds', 'X', 'Y', 'Speed', 'Distance', 'Throttle', 'Brake', 'LapNumber']
    telemetry[output_columns].to_csv(full_path, index=False)

    print(f"Successfully exported pole lap telemetry to: {full_path}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Fetch pole lap telemetry for a given F1 GP.")
    parser.add_argument("year", type=int, help="The year of the session (e.g., 2023).")
    parser.add_argument("gp", type=str, help="The name of the Grand Prix (e.g., 'Australia').")
    
    args = parser.parse_args()
    
    get_pole_lap(args.year, args.gp)