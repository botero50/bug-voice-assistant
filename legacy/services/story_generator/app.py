from flask import Flask, jsonify, request
import os
import random
from datetime import datetime

app = Flask(__name__)

# Mock story generator (in production, use Claude API)
STORY_THEMES = ["adventure", "mystery", "romance", "sci-fi", "fantasy", "thriller"]
STORY_LENGTH_OPTIONS = ["short", "medium", "long"]

GENERATED_STORIES = {
    "adventure": "Once upon a time, in a land far away, a brave adventurer set out on a perilous journey. With sword in hand and courage in heart, they faced dragons, crossed treacherous mountains, and rescued those in need. The kingdom would forever remember their deeds.",

    "mystery": "In the fog-laden streets of a quiet town, a detective received a mysterious case. Clues led to shadows, shadows to secrets, and secrets to the truth. With a sharp mind and determination, the detective unraveled the darkest of mysteries, bringing justice to those wronged.",

    "romance": "Two hearts met under the moonlit sky, strangers who became soulmates. Through laughter and tears, through storms and sunshine, their love grew stronger with each passing day. In each other's arms, they found home, a love that would endure eternity.",

    "sci-fi": "In the year 2450, humanity had spread across the stars. On a distant planet, an explorer discovered ancient ruins holding secrets of a long-lost civilization. The discovery would change humanity's understanding of the universe and our place within it.",

    "fantasy": "In the realm of magic and wonder, a young wizard discovered ancient powers within themselves. With friends by their side and spells at their fingertips, they rose to challenge the darkness that threatened their world. Magic flowed through their veins, destiny in their hands.",

    "thriller": "Pursued through the night by unseen enemies, a fugitive ran for their life. Every corner held danger, every shadow a threat. But with intelligence and courage, they turned the tables, exposing the truth and reclaiming their freedom.",
}

def generate_mock_story(theme="random", length="medium"):
    """Generate a mock story (demo purposes)"""
    if theme == "random":
        theme = random.choice(STORY_THEMES)

    story = GENERATED_STORIES.get(theme, GENERATED_STORIES["adventure"])

    # Expand for length
    if length == "long":
        story = story + " " + story
    elif length == "short":
        story = story[:len(story)//2]

    return story, theme

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "story_generator"}), 200

@app.route('/story/generate', methods=['POST'])
def generate_story():
    """Generate a story (mock or Claude API)"""
    try:
        data = request.get_json() or {}
        theme = data.get("theme", "random")
        length = data.get("length", "medium")

        if theme not in STORY_THEMES and theme != "random":
            return jsonify({"error": f"Invalid theme. Choose from: {', '.join(STORY_THEMES)}"}), 400

        if length not in STORY_LENGTH_OPTIONS:
            return jsonify({"error": f"Invalid length. Choose from: {', '.join(STORY_LENGTH_OPTIONS)}"}), 400

        story, used_theme = generate_mock_story(theme, length)

        return jsonify({
            "story": story,
            "theme": used_theme,
            "length": length,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/story/themes', methods=['GET'])
def get_themes():
    """Get available story themes"""
    return jsonify({
        "themes": STORY_THEMES,
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/story/lengths', methods=['GET'])
def get_lengths():
    """Get available story lengths"""
    return jsonify({
        "lengths": STORY_LENGTH_OPTIONS,
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/story/example/<theme>', methods=['GET'])
def get_example(theme):
    """Get an example story for a theme"""
    if theme not in STORY_THEMES:
        return jsonify({"error": f"Theme not found. Choose from: {', '.join(STORY_THEMES)}"}), 404

    story = GENERATED_STORIES.get(theme, "")
    return jsonify({
        "theme": theme,
        "story": story,
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/story/continue', methods=['POST'])
def continue_story():
    """Continue an existing story (mock)"""
    try:
        data = request.get_json() or {}
        current_story = data.get("story", "")

        continuation = " As the tale continued, unexpected turns of fate emerged..."

        return jsonify({
            "story": current_story + continuation,
            "continued": True,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    print("📖 Story Generator Service Starting...")
    print(f"📚 Available themes: {', '.join(STORY_THEMES)}")
    print("💡 Currently using mock stories (integrate Claude API for production)")
    app.run(host='0.0.0.0', port=5112, debug=False)
