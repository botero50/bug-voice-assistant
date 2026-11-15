from flask import Flask, request, jsonify
from flask_cors import CORS
import os
import logging

app = Flask(__name__)
CORS(app)

logging.basicConfig(level=os.getenv('LOG_LEVEL', 'INFO'))
logger = logging.getLogger(__name__)

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'service': 'calendar_merger'}), 200

@app.route('/<path:path>', methods=['GET', 'POST', 'PUT', 'DELETE'])
def catch_all(path):
    return jsonify({
        'service': 'calendar_merger',
        'endpoint': path,
        'mock': True,
        'status': 'ok'
    }), 200

if __name__ == '__main__':
    logger.info("calendar_merger service running")
    app.run(host='0.0.0.0', port=5000, debug=False)
