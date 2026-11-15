from flask import Flask, jsonify, request
import random
from datetime import datetime

app = Flask(__name__)

# Akinator-style game: 20 questions to guess what you're thinking
# Questions progress from general to specific

QUESTIONS = [
    {"id": 1, "question": "¿Es una persona?", "type": "yes_no"},
    {"id": 2, "question": "¿Está vivo?", "type": "yes_no"},
    {"id": 3, "question": "¿Es un animal?", "type": "yes_no"},
    {"id": 4, "question": "¿Se puede comer?", "type": "yes_no"},
    {"id": 5, "question": "¿Es más grande que una mano?", "type": "yes_no"},
    {"id": 6, "question": "¿Se mueve rápido?", "type": "yes_no"},
    {"id": 7, "question": "¿Tiene alas?", "type": "yes_no"},
    {"id": 8, "question": "¿Vive en el agua?", "type": "yes_no"},
    {"id": 9, "question": "¿Tiene dientes?", "type": "yes_no"},
    {"id": 10, "question": "¿Es doméstico?", "type": "yes_no"},
    {"id": 11, "question": "¿Es un objeto de uso diario?", "type": "yes_no"},
    {"id": 12, "question": "¿Está hecho de metal?", "type": "yes_no"},
    {"id": 13, "question": "¿Se puede transportar fácilmente?", "type": "yes_no"},
    {"id": 14, "question": "¿Cuesta menos de 100 dólares?", "type": "yes_no"},
    {"id": 15, "question": "¿Está en una casa?", "type": "yes_no"},
    {"id": 16, "question": "¿Se usa para dormir?", "type": "yes_no"},
    {"id": 17, "question": "¿Es coloreado?", "type": "yes_no"},
    {"id": 18, "question": "¿Necesita electricidad?", "type": "yes_no"},
    {"id": 19, "question": "¿Tiene botones?", "type": "yes_no"},
    {"id": 20, "question": "¿Lo usas todos los días?", "type": "yes_no"},
]

# Database of guesses (things the Akinator can guess)
GUESSES_DATABASE = [
    {
        "name": "Gato",
        "description": "Un felino doméstico",
        "characteristics": {
            "persona": False,
            "vivo": True,
            "animal": True,
            "comestible": False,
            "grande": False,
            "rapido": True,
            "alas": False,
            "agua": False,
            "dientes": True,
            "domestico": True
        }
    },
    {
        "name": "Teléfono",
        "description": "Dispositivo para comunicarse",
        "characteristics": {
            "persona": False,
            "vivo": False,
            "animal": False,
            "comestible": False,
            "grande": False,
            "rapido": False,
            "alas": False,
            "agua": False,
            "dientes": False,
            "domestico": True,
            "objeto_diario": True,
            "metal": True,
            "transportable": True,
            "caro": True,
            "casa": True,
            "dormir": False,
            "coloreado": True,
            "electricidad": True,
            "botones": True,
            "diario": True
        }
    },
    {
        "name": "Cama",
        "description": "Mueble para dormir",
        "characteristics": {
            "persona": False,
            "vivo": False,
            "animal": False,
            "comestible": False,
            "grande": True,
            "rapido": False,
            "alas": False,
            "agua": False,
            "dientes": False,
            "domestico": False,
            "objeto_diario": True,
            "metal": True,
            "transportable": False,
            "caro": False,
            "casa": True,
            "dormir": True,
            "coloreado": True,
            "electricidad": False,
            "botones": False,
            "diario": True
        }
    },
    {
        "name": "Manzana",
        "description": "Fruta roja comestible",
        "characteristics": {
            "persona": False,
            "vivo": True,
            "animal": False,
            "comestible": True,
            "grande": False,
            "rapido": False,
            "alas": False,
            "agua": False,
            "dientes": False,
            "domestico": False
        }
    },
    {
        "name": "Perro",
        "description": "Animal doméstico leal",
        "characteristics": {
            "persona": False,
            "vivo": True,
            "animal": True,
            "comestible": False,
            "grande": True,
            "rapido": True,
            "alas": False,
            "agua": False,
            "dientes": True,
            "domestico": True
        }
    },
]

# Game state storage (session-based in production)
game_sessions = {}

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "akinator"}), 200

@app.route('/game/start', methods=['POST'])
def start_game():
    """Start a new Akinator game"""
    try:
        session_id = f"session_{datetime.now().timestamp()}"
        game_sessions[session_id] = {
            "question_index": 0,
            "answers": [],
            "game_active": True,
            "start_time": datetime.now().isoformat()
        }

        next_question = QUESTIONS[0]
        return jsonify({
            "session_id": session_id,
            "question": next_question["question"],
            "question_number": 1,
            "total_questions": len(QUESTIONS),
            "hint": "Think of something and answer yes/no to my questions.",
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/game/<session_id>/answer', methods=['POST'])
def answer_question(session_id):
    """Submit answer to current question"""
    try:
        if session_id not in game_sessions:
            return jsonify({"error": "Session not found"}), 404

        data = request.get_json() or {}
        answer = data.get("answer", "").lower()  # yes/no

        if answer not in ["yes", "no", "si", "no"]:
            return jsonify({"error": "Answer must be 'yes' or 'no'"}), 400

        session = game_sessions[session_id]
        question_index = session["question_index"]
        session["answers"].append(answer)

        # Check if we've asked all questions
        if question_index >= len(QUESTIONS) - 1:
            # Make a guess based on answers
            guess = make_guess(session["answers"])
            session["game_active"] = False

            return jsonify({
                "game_over": True,
                "guess": guess["name"],
                "description": guess["description"],
                "confidence": "95%",
                "message": f"¡Creo que estabas pensando en {guess['name']}!",
                "timestamp": datetime.now().isoformat()
            }), 200

        # Move to next question
        session["question_index"] = question_index + 1
        next_question = QUESTIONS[question_index + 1]

        return jsonify({
            "game_over": False,
            "question": next_question["question"],
            "question_number": question_index + 2,
            "total_questions": len(QUESTIONS),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/game/<session_id>/cheat', methods=['GET'])
def reveal_answer(session_id):
    """Reveal what the player was thinking (cheat/help)"""
    try:
        if session_id not in game_sessions:
            return jsonify({"error": "Session not found"}), 404

        session = game_sessions[session_id]
        guess = make_guess(session["answers"])

        return jsonify({
            "was_thinking_of": guess["name"],
            "description": guess["description"],
            "confidence": "85%",
            "hint": f"Your answers suggested: {', '.join(session['answers'][:5])}...",
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

def make_guess(answers):
    """Simple AI to guess what player is thinking based on answers"""
    # In production, use actual Akinator API or ML model
    # For now, use pattern matching

    if not answers:
        return random.choice(GUESSES_DATABASE)

    # Simple matching based on first few answers
    if len(answers) >= 1:
        if answers[0] in ["si", "yes"]:
            # Thinking of a person
            return {"name": "Persona", "description": "Un ser humano"}

    if len(answers) >= 2:
        if answers[0] in ["si", "yes"] and answers[1] in ["si", "yes"]:
            # Alive
            if len(answers) >= 3 and answers[2] in ["si", "yes"]:
                # Animal
                return random.choice([g for g in GUESSES_DATABASE if g["name"] in ["Gato", "Perro"]])
        elif answers[0] in ["no"] and answers[1] in ["no"]:
            # Not alive, not a person
            return random.choice([g for g in GUESSES_DATABASE if g["name"] in ["Teléfono", "Cama"]])

    # Default: random from database
    return random.choice(GUESSES_DATABASE)

@app.route('/stats', methods=['GET'])
def get_stats():
    """Get game statistics"""
    active_games = sum(1 for s in game_sessions.values() if s["game_active"])
    completed_games = sum(1 for s in game_sessions.values() if not s["game_active"])

    return jsonify({
        "active_games": active_games,
        "completed_games": completed_games,
        "total_sessions": len(game_sessions),
        "questions_available": len(QUESTIONS),
        "guesses_database": len(GUESSES_DATABASE),
        "timestamp": datetime.now().isoformat()
    }), 200

if __name__ == '__main__':
    print("🎮 Akinator Game Service Starting...")
    print(f"📚 Database: {len(GUESSES_DATABASE)} guesses")
    print(f"❓ Questions: {len(QUESTIONS)} total")
    app.run(host='0.0.0.0', port=5115, debug=False)
