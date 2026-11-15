from flask import Flask, request, jsonify
from flask_cors import CORS
import os
import logging
import base64
import time

app = Flask(__name__)
CORS(app)

logging.basicConfig(level=os.getenv('LOG_LEVEL', 'INFO'))
logger = logging.getLogger(__name__)

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'service': 'tts'}), 200

@app.route('/synthesize', methods=['POST'])
def synthesize():
    try:
        data = request.json
        text = data.get('text', '')
        
        # Mock WAV data (minimal valid WAV header)
        mock_wav = b'RIFF$\x00\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00D\xac\x00\x00\x88X\x01\x00\x02\x00\x10\x00data\x00\x00\x00\x00'
        audio_base64 = base64.b64encode(mock_wav).decode('utf-8')
        
        logger.info(f"Synthesized (mock): {text[:50]}...")
        
        return jsonify({
            'audio_base64': audio_base64,
            'processing_time_ms': 800
        }), 200
    except Exception as e:
        logger.error(f"TTS error: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    logger.info("TTS Service on port 5003")
    app.run(host='0.0.0.0', port=5003, debug=False)
