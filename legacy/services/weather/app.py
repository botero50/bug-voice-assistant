from flask import Flask, jsonify, request
import requests
import json
from datetime import datetime
import time

app = Flask(__name__)

# Open-Meteo API (free, no auth required)
OPEN_METEO_API = "https://api.open-meteo.com/v1/forecast"
GEOCODING_API = "https://geocoding-api.open-meteo.com/v1/search"

# Default location (Colombia)
DEFAULT_LAT = 4.7110
DEFAULT_LON = -74.0721  # Bogotá
DEFAULT_CITY = "Bogotá"

# Cache for recent weather data
cache = {
    "data": {},
    "timestamp": 0,
    "ttl": 3600  # 1 hour cache
}

def get_weather_cached(lat, lon, city=""):
    """Fetch weather with caching"""
    now = time.time()
    cache_key = f"{lat},{lon}"

    if cache_key in cache["data"] and (now - cache["timestamp"]) < cache["ttl"]:
        return cache["data"][cache_key]

    try:
        response = requests.get(
            OPEN_METEO_API,
            params={
                "latitude": lat,
                "longitude": lon,
                "current": "temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m",
                "hourly": "temperature_2m,precipitation",
                "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum",
                "timezone": "auto",
                "language": "es"
            },
            timeout=5
        )
        response.raise_for_status()
        data = response.json()

        # Format response
        current = data.get("current", {})
        daily = data.get("daily", {})

        weather_data = {
            "city": city or f"{lat},{lon}",
            "current": {
                "temperature": current.get("temperature_2m"),
                "feels_like": current.get("apparent_temperature"),
                "humidity": current.get("relative_humidity_2m"),
                "precipitation": current.get("precipitation"),
                "wind_speed": current.get("wind_speed_10m"),
                "description": get_weather_description(current.get("weather_code"))
            },
            "daily": {
                "max_temp": daily.get("temperature_2m_max", [None])[0],
                "min_temp": daily.get("temperature_2m_min", [None])[0],
                "precipitation": daily.get("precipitation_sum", [None])[0],
                "forecast": [
                    {
                        "date": daily.get("time", [None])[i] if i < len(daily.get("time", [])) else None,
                        "max": daily.get("temperature_2m_max", [None])[i] if i < len(daily.get("temperature_2m_max", [])) else None,
                        "min": daily.get("temperature_2m_min", [None])[i] if i < len(daily.get("temperature_2m_min", [])) else None,
                        "description": get_weather_description(daily.get("weather_code", [None])[i] if i < len(daily.get("weather_code", [])) else None)
                    }
                    for i in range(min(7, len(daily.get("time", []))))
                ]
            },
            "timestamp": datetime.now().isoformat()
        }

        cache["data"][cache_key] = weather_data
        cache["timestamp"] = now

        return weather_data
    except Exception as e:
        print(f"Error fetching weather: {e}")
        return {"error": str(e), "fallback": True}

def geocode_city(city_name):
    """Convert city name to coordinates"""
    try:
        response = requests.get(
            GEOCODING_API,
            params={
                "name": city_name,
                "count": 1,
                "language": "es",
                "format": "json"
            },
            timeout=5
        )
        response.raise_for_status()
        results = response.json().get("results", [])

        if results:
            result = results[0]
            return {
                "lat": result.get("latitude"),
                "lon": result.get("longitude"),
                "city": result.get("name"),
                "country": result.get("country")
            }
        return None
    except Exception as e:
        print(f"Error geocoding: {e}")
        return None

def get_weather_description(code):
    """Convert WMO weather code to description"""
    code_map = {
        0: "Despejado",
        1: "Principalmente despejado",
        2: "Parcialmente nublado",
        3: "Nublado",
        45: "Neblina",
        48: "Neblina depositada",
        51: "Llovizna ligera",
        53: "Llovizna moderada",
        55: "Llovizna densa",
        61: "Lluvia ligera",
        63: "Lluvia moderada",
        65: "Lluvia densa",
        71: "Nieve ligera",
        73: "Nieve moderada",
        75: "Nieve densa",
        77: "Granos de nieve",
        80: "Aguaceros ligeros",
        81: "Aguaceros moderados",
        82: "Aguaceros violentos",
        85: "Aguaceros de nieve ligeros",
        86: "Aguaceros de nieve fuertes",
        95: "Tormenta con granizo",
        96: "Tormenta con granizo ligero",
        99: "Tormenta con granizo fuerte"
    }
    return code_map.get(code, "Desconocido")

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "weather"}), 200

@app.route('/weather/current', methods=['GET'])
def get_current_weather():
    """Get current weather for default location or specified city"""
    try:
        city = request.args.get('city', DEFAULT_CITY)
        lat = request.args.get('lat', DEFAULT_LAT)
        lon = request.args.get('lon', DEFAULT_LON)

        # If city name provided, geocode it
        if city != DEFAULT_CITY:
            geocoded = geocode_city(city)
            if geocoded:
                lat, lon = geocoded["lat"], geocoded["lon"]
                city = geocoded["city"]

        weather = get_weather_cached(float(lat), float(lon), city)

        return jsonify(weather), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/weather/forecast', methods=['GET'])
def get_forecast():
    """Get 7-day forecast"""
    try:
        city = request.args.get('city', DEFAULT_CITY)
        lat = request.args.get('lat', DEFAULT_LAT)
        lon = request.args.get('lon', DEFAULT_LON)

        if city != DEFAULT_CITY:
            geocoded = geocode_city(city)
            if geocoded:
                lat, lon = geocoded["lat"], geocoded["lon"]
                city = geocoded["city"]

        weather = get_weather_cached(float(lat), float(lon), city)

        return jsonify({
            "city": weather.get("city"),
            "forecast": weather.get("daily", {}).get("forecast", []),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/weather/city/<city_name>', methods=['GET'])
def weather_by_city(city_name):
    """Get weather for a specific city"""
    try:
        geocoded = geocode_city(city_name)

        if not geocoded:
            return jsonify({"error": f"City '{city_name}' not found"}), 404

        weather = get_weather_cached(geocoded["lat"], geocoded["lon"], geocoded["city"])

        return jsonify({
            **weather,
            "country": geocoded.get("country")
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/weather/alert', methods=['GET'])
def weather_alert():
    """Check if severe weather alert needed"""
    try:
        city = request.args.get('city', DEFAULT_CITY)
        lat = request.args.get('lat', DEFAULT_LAT)
        lon = request.args.get('lon', DEFAULT_LON)

        if city != DEFAULT_CITY:
            geocoded = geocode_city(city)
            if geocoded:
                lat, lon = geocoded["lat"], geocoded["lon"]

        weather = get_weather_cached(float(lat), float(lon), city)
        current = weather.get("current", {})

        alerts = []
        if current.get("precipitation", 0) > 10:
            alerts.append("Lluvia intensa esperada")
        if current.get("wind_speed", 0) > 40:
            alerts.append("Vientos fuertes")
        if "Tormenta" in current.get("description", ""):
            alerts.append("Tormenta detectada")

        return jsonify({
            "alerts": alerts if alerts else ["Sin alertas severas"],
            "city": city,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    print("🌤️ Weather Service Starting...")
    print("🌐 Using Open-Meteo API (free, no auth required)")
    print(f"📍 Default location: {DEFAULT_CITY} ({DEFAULT_LAT}, {DEFAULT_LON})")
    app.run(host='0.0.0.0', port=5116, debug=False)
