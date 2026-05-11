import fastf1

# Load the 2025 Italian Grand Prix Race session
session = fastf1.get_session(2025, 'Monza', 'R')
session.load()

# Get Max Verstappen's fastest lap (or any specific lap number)
ver_lap = session.laps.pick_driver('VER').pick_fastest()

# Get the telemetry data
telemetry = ver_lap.get_telemetry()

# Export for your app
telemetry[['X', 'Y', 'Speed', 'Distance', 'Throttle', 'Brake']].to_csv('monza_2025_ver_telemetry.csv')