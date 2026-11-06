from flask import Flask, request, jsonify
from flask_cors import CORS
import os
import logging
import time

app = Flask(__name__)
CORS(app)

logging.basicConfig(level=os.getenv('LOG_LEVEL', 'INFO'))
logger = logging.getLogger(__name__)

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'service': 'llm'}), 200

@app.route('/chat', methods=['POST'])
def chat():
    try:
        data = request.json
        message = data.get('message', '')
        
        # Mock Claude response in Spanish
        responses = {
            "¿Cuál es tu nombre?": "Soy Bug, una asistente inteligente colombiana. ¿En qué puedo ayudarte?",
            "Hola Bug, ¿cómo estás?": "¡Hola! Estoy muy bien, gracias por preguntar. ¿Y tú cómo estás?",
            "¿Qué hora es?": "No puedo ver la hora exacta, pero puedes verificarla en tu dispositivo.",
        }
        
        response_text = responses.get(message, f"Recibí tu mensaje: '{message}'. ¿Cómo puedo ayudarte?")
        
        logger.info(f"LLM response (mock): {response_text}")
        
        return jsonify({
            'response': response_text,
            'processing_time_ms': 1000
        }), 200
    except Exception as e:
        logger.error(f"LLM error: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    logger.info("LLM Service on port 5002")
    app.run(host='0.0.0.0', port=5002, debug=False)
