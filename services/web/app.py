from flask import Flask, render_template, jsonify, request
from flask_cors import CORS
import os
import logging

app = Flask(__name__, template_folder='templates')
CORS(app)

logging.basicConfig(level=os.getenv('LOG_LEVEL', 'INFO'))
logger = logging.getLogger(__name__)

@app.route('/', methods=['GET'])
def index():
    return render_template('index.html')

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'service': 'web'}), 200

if __name__ == '__main__':
    logger.info("Web UI on port 8080")
    app.run(host='0.0.0.0', port=8080, debug=False)
