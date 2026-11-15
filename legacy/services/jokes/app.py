from flask import Flask, jsonify, request
import random
import json
import os
from datetime import datetime

app = Flask(__name__)

# Jokes database
JOKES_DB = [
    {
        "category": "programming",
        "setup": "Why do programmers prefer dark mode?",
        "punchline": "Because light attracts bugs!"
    },
    {
        "category": "programming",
        "setup": "How many programmers does it take to change a light bulb?",
        "punchline": "None, that's a hardware problem."
    },
    {
        "category": "programming",
        "setup": "Why did the developer go broke?",
        "punchline": "Because he used up all his cache!"
    },
    {
        "category": "programming",
        "setup": "Why did the CLI go to school?",
        "punchline": "To improve its class structure!"
    },
    {
        "category": "programming",
        "setup": "What's the object-oriented way to become wealthy?",
        "punchline": "Inheritance!"
    },
    {
        "category": "general",
        "setup": "Why did the scarecrow win an award?",
        "punchline": "Because he was outstanding in his field!"
    },
    {
        "category": "general",
        "setup": "What do you call a fake noodle?",
        "punchline": "An impasta!"
    },
    {
        "category": "general",
        "setup": "Why don't scientists trust atoms?",
        "punchline": "Because they make up everything!"
    },
    {
        "category": "general",
        "setup": "Did you hear about the mathematician who's afraid of negative numbers?",
        "punchline": "He'll stop at nothing to avoid them!"
    },
    {
        "category": "general",
        "setup": "What did one wall say to the other?",
        "punchline": "I'll meet you at the corner!"
    },
]

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "jokes"}), 200

@app.route('/joke/random', methods=['GET'])
def get_random_joke():
    """Get a random joke"""
    joke = random.choice(JOKES_DB)
    return jsonify({
        "setup": joke["setup"],
        "punchline": joke["punchline"],
        "category": joke["category"],
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/joke/category/<category>', methods=['GET'])
def get_joke_by_category(category):
    """Get a random joke from a specific category"""
    category = category.lower()
    matching = [j for j in JOKES_DB if j["category"] == category]

    if not matching:
        return jsonify({"error": "Category not found"}), 404

    joke = random.choice(matching)
    return jsonify({
        "setup": joke["setup"],
        "punchline": joke["punchline"],
        "category": joke["category"],
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/joke/categories', methods=['GET'])
def get_categories():
    """Get available joke categories"""
    categories = list(set(j["category"] for j in JOKES_DB))
    return jsonify({"categories": sorted(categories)}), 200

@app.route('/joke/all', methods=['GET'])
def get_all_jokes():
    """Get all jokes (for management)"""
    return jsonify({"jokes": JOKES_DB, "total": len(JOKES_DB)}), 200

@app.route('/stats', methods=['GET'])
def get_stats():
    """Get jokes database statistics"""
    categories = {}
    for joke in JOKES_DB:
        cat = joke["category"]
        categories[cat] = categories.get(cat, 0) + 1

    return jsonify({
        "total_jokes": len(JOKES_DB),
        "categories": categories,
        "timestamp": datetime.now().isoformat()
    }), 200

if __name__ == '__main__':
    print("🤣 Jokes Service Starting...")
    print(f"📚 Loaded {len(JOKES_DB)} jokes across {len(set(j['category'] for j in JOKES_DB))} categories")
    app.run(host='0.0.0.0', port=5106, debug=False)
