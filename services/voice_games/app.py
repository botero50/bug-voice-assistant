from flask import Flask, jsonify, request
import random
from datetime import datetime

app = Flask(__name__)

# Trivia questions
TRIVIA_QUESTIONS = [
    {
        "question": "What is the capital of France?",
        "options": ["Paris", "London", "Berlin", "Madrid"],
        "correct": "Paris",
        "category": "geography"
    },
    {
        "question": "What is the largest planet in our solar system?",
        "options": ["Mars", "Jupiter", "Saturn", "Neptune"],
        "correct": "Jupiter",
        "category": "science"
    },
    {
        "question": "In what year did the Titanic sink?",
        "options": ["1912", "1920", "1905", "1915"],
        "correct": "1912",
        "category": "history"
    },
    {
        "question": "What is the smallest country in the world?",
        "options": ["Monaco", "Vatican City", "Liechtenstein", "San Marino"],
        "correct": "Vatican City",
        "category": "geography"
    },
    {
        "question": "What is the chemical symbol for Gold?",
        "options": ["Go", "Gd", "Au", "Ag"],
        "correct": "Au",
        "category": "science"
    },
]

# Riddles
RIDDLES = [
    {
        "riddle": "I speak without a mouth and hear without ears. I have no body, but I come alive with wind. What am I?",
        "answer": "echo",
        "hint": "It's a sound phenomenon"
    },
    {
        "riddle": "What has keys but no locks, space but no room, and you can enter but can't go inside?",
        "answer": "keyboard",
        "hint": "It's used for typing"
    },
    {
        "riddle": "The more you take, the more you leave behind. What am I?",
        "answer": "footsteps",
        "hint": "Related to walking"
    },
    {
        "riddle": "I have cities, but no houses. I have forests, but no trees. What am I?",
        "answer": "map",
        "hint": "Used for navigation"
    },
    {
        "riddle": "What gets wet while drying?",
        "answer": "towel",
        "hint": "Used after bathing"
    },
]

# Word association game data
WORD_PAIRS = [
    {"word": "cat", "hint": "feline animal"},
    {"word": "ocean", "hint": "body of water"},
    {"word": "mountain", "hint": "tall landform"},
    {"word": "book", "hint": "reading material"},
    {"word": "music", "hint": "sound art form"},
]

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "voice_games"}), 200

@app.route('/trivia/random', methods=['GET'])
def get_random_trivia():
    """Get a random trivia question"""
    question = random.choice(TRIVIA_QUESTIONS)
    return jsonify({
        "question": question["question"],
        "options": question["options"],
        "category": question["category"],
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/trivia/check', methods=['POST'])
def check_trivia_answer():
    """Check trivia answer"""
    try:
        data = request.get_json() or {}
        question = data.get("question", "")
        user_answer = data.get("answer", "").lower()

        # Find matching question
        for q in TRIVIA_QUESTIONS:
            if q["question"] == question:
                correct_answer = q["correct"].lower()
                is_correct = user_answer == correct_answer

                return jsonify({
                    "correct": is_correct,
                    "correct_answer": q["correct"],
                    "feedback": "Great job!" if is_correct else "Sorry, try again!",
                    "timestamp": datetime.now().isoformat()
                }), 200

        return jsonify({"error": "Question not found"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/riddle/random', methods=['GET'])
def get_random_riddle():
    """Get a random riddle"""
    riddle = random.choice(RIDDLES)
    return jsonify({
        "riddle": riddle["riddle"],
        "hint": riddle["hint"],
        "difficulty": "medium",
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/riddle/check', methods=['POST'])
def check_riddle_answer():
    """Check riddle answer"""
    try:
        data = request.get_json() or {}
        riddle_text = data.get("riddle", "")
        user_answer = data.get("answer", "").lower().strip()

        # Find matching riddle
        for r in RIDDLES:
            if r["riddle"] == riddle_text:
                correct_answer = r["answer"].lower()
                is_correct = user_answer == correct_answer

                return jsonify({
                    "correct": is_correct,
                    "correct_answer": r["answer"],
                    "feedback": "You got it!" if is_correct else "Not quite!",
                    "timestamp": datetime.now().isoformat()
                }), 200

        return jsonify({"error": "Riddle not found"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/word-game/random', methods=['GET'])
def get_word_game():
    """Get a random word for word game"""
    word = random.choice(WORD_PAIRS)
    return jsonify({
        "hint": word["hint"],
        "word": word["word"],  # For testing; in real game, don't send this
        "timestamp": datetime.now().isoformat()
    }), 200

@app.route('/word-game/check', methods=['POST'])
def check_word_answer():
    """Check word game answer"""
    try:
        data = request.get_json() or {}
        hint = data.get("hint", "")
        user_answer = data.get("answer", "").lower().strip()

        # Find matching word
        for w in WORD_PAIRS:
            if w["hint"] == hint:
                correct_word = w["word"].lower()
                is_correct = user_answer == correct_word

                return jsonify({
                    "correct": is_correct,
                    "correct_word": w["word"],
                    "feedback": "Correct!" if is_correct else "Try again!",
                    "timestamp": datetime.now().isoformat()
                }), 200

        return jsonify({"error": "Hint not found"}), 404
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/games/stats', methods=['GET'])
def get_stats():
    """Get game statistics"""
    return jsonify({
        "trivia_questions": len(TRIVIA_QUESTIONS),
        "riddles": len(RIDDLES),
        "word_games": len(WORD_PAIRS),
        "timestamp": datetime.now().isoformat()
    }), 200

if __name__ == '__main__':
    print("🎮 Voice Games Service Starting...")
    print(f"📋 Trivia: {len(TRIVIA_QUESTIONS)} questions")
    print(f"🧩 Riddles: {len(RIDDLES)} riddles")
    print(f"📝 Word Games: {len(WORD_PAIRS)} words")
    app.run(host='0.0.0.0', port=5114, debug=False)
