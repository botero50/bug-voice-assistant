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
    return jsonify({'status': 'healthy', 'service': 'stt'}), 200

@app.route('/transcribe', methods=['POST'])
def transcribe():
    try:
        start_time = time.time()
        mock_responses = [
            "¿Cuál es tu nombre?",
            "Hola Bug, ¿cómo estás?",
            "¿Qué hora es?",
        ]
        transcribed_text = mock_responses[0]
        processing_time = 500
        logger.info(f"Transcribed (mock): {transcribed_text}")
        return jsonify({
            'text': transcribed_text,
            'confidence': 0.95,
            'processing_time_ms': processing_time
        }), 200
    except Exception as e:
        logger.error(f"STT error: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    logger.info("STT Service on port 5001")
    app.run(host='0.0.0.0', port=5001, debug=False)
