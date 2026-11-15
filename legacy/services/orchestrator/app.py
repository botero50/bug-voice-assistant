from flask import Flask, request, jsonify
from flask_cors import CORS
import os
import logging
import requests

app = Flask(__name__)
CORS(app)

logging.basicConfig(level=os.getenv('LOG_LEVEL', 'INFO'))
logger = logging.getLogger(__name__)

WAKE_WORD_SERVICE = os.getenv('WAKE_WORD_SERVICE', 'http://localhost:5105')

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'service': 'orchestrator'}), 200

@app.route('/process', methods=['POST'])
def process_voice():
    try:
        data = request.json

        response = {
            'user_input': '¿Cuál es tu nombre?',
            'response_text': 'Soy Bug, una asistente inteligente colombiana',
            'audio_base64': 'UklGRiYAAABXQVZFZm10IBAAAAABAAEAQB8AAAB9AAACABAAZGF0YQIAAAAAAA==',
            'latency': {'stt_ms': 500, 'llm_ms': 1000, 'tts_ms': 800, 'total_ms': 2300}
        }

        logger.info("Voice processing complete (mock)")
        return jsonify(response), 200
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/wake-word/detect', methods=['POST'])
def detect_wake_word():
    try:
        data = request.json
        audio_data = data.get('audio_samples', [])

        # Forward to wake_word service
        resp = requests.post(f'{WAKE_WORD_SERVICE}/detect', json={'audio_samples': audio_data}, timeout=2)
        return jsonify(resp.json()), resp.status_code
    except Exception as e:
        logger.error(f"Wake word detection error: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/wake-word/stats', methods=['GET'])
def wake_word_stats():
    try:
        resp = requests.get(f'{WAKE_WORD_SERVICE}/stats', timeout=2)
        return jsonify(resp.json()), resp.status_code
    except Exception as e:
        logger.error(f"Wake word stats error: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    logger.info("Orchestrator on port 5000")
    app.run(host='0.0.0.0', port=5000, debug=False)
