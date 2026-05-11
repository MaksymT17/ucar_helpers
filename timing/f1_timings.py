import fastf1

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
    
    if not driver_laps.empty:
        # Get telemetry for all laps combined
        telemetry = driver_laps.get_telemetry()
        
        # Export to a separate CSV file named by year, GP, and driver abbreviation
        filename = f'australia_2026_{abb}_telemetry.csv'
        telemetry[['X', 'Y', 'Speed', 'Distance', 'Throttle', 'Brake']].to_csv(filename)
        print(f"Exported telemetry for {abb} to {filename}")