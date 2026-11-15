from flask import Flask, jsonify, request
import requests
import os
import time
from datetime import datetime, timedelta

app = Flask(__name__)

# CoinGecko API (free, no auth required)
COINGECKO_API_URL = "https://api.coingecko.com/api/v3"
CACHE_TTL = 300  # 5 minutes

# In-memory cache
cache = {
    "data": {},
    "timestamp": 0
}

def get_cached_prices():
    """Fetch crypto prices with caching"""
    now = time.time()
    if cache["data"] and (now - cache["timestamp"]) < CACHE_TTL:
        return cache["data"]

    try:
        response = requests.get(
            f"{COINGECKO_API_URL}/simple/price",
            params={
                "ids": "bitcoin,ethereum,cardano",
                "vs_currencies": "usd",
                "include_market_cap": "true",
                "include_24hr_change": "true"
            },
            timeout=5
        )
        response.raise_for_status()
        data = response.json()

        cache["data"] = data
        cache["timestamp"] = now
        return data
    except Exception as e:
        print(f"Error fetching crypto prices: {e}")
        return cache.get("data", {})

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "crypto"}), 200

@app.route('/prices', methods=['GET'])
def get_prices():
    """Get current crypto prices (BTC, ETH, ADA)"""
    try:
        prices = get_cached_prices()

        return jsonify({
            "bitcoin": prices.get("bitcoin", {}).get("usd"),
            "ethereum": prices.get("ethereum", {}).get("usd"),
            "cardano": prices.get("cardano", {}).get("usd"),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/prices/all', methods=['GET'])
def get_all_prices():
    """Get all cached crypto data"""
    try:
        prices = get_cached_prices()
        return jsonify({
            "data": prices,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/price/<crypto>', methods=['GET'])
def get_price(crypto):
    """Get price for a specific crypto"""
    try:
        crypto = crypto.lower()
        prices = get_cached_prices()

        if crypto not in prices:
            # Try to fetch individual crypto
            try:
                response = requests.get(
                    f"{COINGECKO_API_URL}/simple/price",
                    params={
                        "ids": crypto,
                        "vs_currencies": "usd",
                        "include_24hr_change": "true"
                    },
                    timeout=5
                )
                response.raise_for_status()
                prices = response.json()
            except:
                return jsonify({"error": f"Crypto '{crypto}' not found"}), 404

        if crypto not in prices:
            return jsonify({"error": f"Crypto '{crypto}' not found"}), 404

        price_data = prices[crypto]
        return jsonify({
            "crypto": crypto,
            "price_usd": price_data.get("usd"),
            "change_24h": price_data.get("usd_24h_change"),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/market-cap', methods=['GET'])
def get_market_cap():
    """Get market cap data"""
    try:
        response = requests.get(
            f"{COINGECKO_API_URL}/global",
            timeout=5
        )
        response.raise_for_status()
        data = response.json()

        btc_market_cap = data.get("data", {}).get("total_market_cap", {}).get("usd")
        btc_dominance = data.get("data", {}).get("btc_dominance")

        return jsonify({
            "total_market_cap_usd": btc_market_cap,
            "bitcoin_dominance": btc_dominance,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/trending', methods=['GET'])
def get_trending():
    """Get trending cryptos (top gainers)"""
    try:
        response = requests.get(
            f"{COINGECKO_API_URL}/search/trending",
            timeout=5
        )
        response.raise_for_status()
        data = response.json()

        trending = []
        for coin in data.get("coins", [])[:5]:
            coin_data = coin.get("item", {})
            trending.append({
                "name": coin_data.get("name"),
                "symbol": coin_data.get("symbol"),
                "market_cap_rank": coin_data.get("market_cap_rank")
            })

        return jsonify({
            "trending": trending,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    print("💹 Crypto Service Starting...")
    print("🔌 Using CoinGecko API (free, no auth required)")
    print(f"💾 Cache TTL: {CACHE_TTL}s")
    app.run(host='0.0.0.0', port=5107, debug=False)
