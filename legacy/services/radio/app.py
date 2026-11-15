from flask import Flask, jsonify, request
import requests
import json
from datetime import datetime

app = Flask(__name__)

# Radio Browser API (free, community-driven)
RADIO_API_URL = "https://de1.api.radio-browser.info/json"

# Popular radio stations (fallback)
FALLBACK_STATIONS = [
    {"name": "BBC Radio 1", "url": "http://bbcmedia.ic.llnwd.net/stream/bbcmedia_live_uk_org_r1_dash_hd_aac", "country": "UK"},
    {"name": "BBC Radio 2", "url": "http://bbcmedia.ic.llnwd.net/stream/bbcmedia_live_uk_org_r2_dash_hd_aac", "country": "UK"},
    {"name": "Classic FM", "url": "http://media-ice.musicgremlin.com/classicfm", "country": "UK"},
    {"name": "WNYC", "url": "https://fm939.wnyc.org/live", "country": "US"},
    {"name": "NPR", "url": "https://npr-ice.streamguys1.com/live.mp3", "country": "US"},
]

def search_stations(query):
    """Search for radio stations"""
    try:
        response = requests.get(
            f"{RADIO_API_URL}/stations/search",
            params={"name": query, "limit": 5},
            timeout=5
        )
        response.raise_for_status()
        return response.json()
    except Exception as e:
        print(f"Error searching stations: {e}")
        return []

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "radio"}), 200

@app.route('/stations/search', methods=['GET'])
def search():
    """Search for radio stations by name"""
    query = request.args.get('query', 'BBC')
    try:
        stations = search_stations(query)
        return jsonify({
            "query": query,
            "stations": stations[:5],
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/stations/popular', methods=['GET'])
def get_popular():
    """Get popular radio stations"""
    try:
        response = requests.get(
            f"{RADIO_API_URL}/stations/topclick",
            params={"limit": 10},
            timeout=5
        )
        response.raise_for_status()
        stations = response.json()

        results = []
        for station in stations:
            results.append({
                "name": station.get("name"),
                "url": station.get("url"),
                "country": station.get("country"),
                "clicks": station.get("clickcount")
            })

        return jsonify({
            "stations": results,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        print(f"Error fetching popular stations: {e}")
        return jsonify({
            "stations": FALLBACK_STATIONS,
            "timestamp": datetime.now().isoformat(),
            "note": "Using fallback stations"
        }), 200

@app.route('/stations/by-country/<country>', methods=['GET'])
def get_by_country(country):
    """Get radio stations by country"""
    try:
        response = requests.get(
            f"{RADIO_API_URL}/stations/bycountry",
            params={"country": country, "limit": 10},
            timeout=5
        )
        response.raise_for_status()
        stations = response.json()

        results = []
        for station in stations:
            results.append({
                "name": station.get("name"),
                "url": station.get("url"),
                "country": station.get("country"),
                "language": station.get("language")
            })

        return jsonify({
            "country": country,
            "stations": results,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        print(f"Error fetching stations by country: {e}")
        return jsonify({"error": str(e)}), 500

@app.route('/stations/by-language/<language>', methods=['GET'])
def get_by_language(language):
    """Get radio stations by language"""
    try:
        response = requests.get(
            f"{RADIO_API_URL}/stations/bylanguage",
            params={"language": language, "limit": 10},
            timeout=5
        )
        response.raise_for_status()
        stations = response.json()

        results = []
        for station in stations:
            results.append({
                "name": station.get("name"),
                "url": station.get("url"),
                "country": station.get("country"),
                "language": station.get("language")
            })

        return jsonify({
            "language": language,
            "stations": results,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        print(f"Error fetching stations by language: {e}")
        return jsonify({"error": str(e)}), 500

@app.route('/station/<int:station_id>', methods=['GET'])
def get_station(station_id):
    """Get specific station details"""
    try:
        response = requests.get(
            f"{RADIO_API_URL}/stations/byid",
            params={"id": station_id},
            timeout=5
        )
        response.raise_for_status()
        station = response.json()[0] if response.json() else None

        if not station:
            return jsonify({"error": "Station not found"}), 404

        return jsonify({
            "name": station.get("name"),
            "url": station.get("url"),
            "country": station.get("country"),
            "language": station.get("language"),
            "bitrate": station.get("bitrate"),
            "codec": station.get("codec"),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/fallback-stations', methods=['GET'])
def fallback_stations():
    """Get fallback station list (for when API is down)"""
    return jsonify({
        "stations": FALLBACK_STATIONS,
        "note": "Use these stations if Radio Browser API is unavailable",
        "timestamp": datetime.now().isoformat()
    }), 200

if __name__ == '__main__':
    print("📻 Radio Service Starting...")
    print("🌐 Using Radio Browser API (community-driven, free)")
    app.run(host='0.0.0.0', port=5111, debug=False)
