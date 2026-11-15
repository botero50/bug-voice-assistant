from flask import Flask, request, jsonify
from flask_cors import CORS
import os
import logging
import json
from datetime import datetime

app = Flask(__name__)
CORS(app)

logging.basicConfig(level=os.getenv('LOG_LEVEL', 'INFO'))
logger = logging.getLogger(__name__)

# Model optimization data (simulated)
MODELS = {
    'wake_word_original': {
        'name': 'Wake Word Detection (Float32)',
        'size_mb': 2.5,
        'latency_ms': 80,
        'accuracy_percent': 95.0,
        'power_ma': 5.0,
        'format': 'float32',
        'optimized': False
    },
    'wake_word_optimized': {
        'name': 'Wake Word Detection (Int8 Quantized)',
        'size_mb': 0.6,
        'latency_ms': 25,
        'accuracy_percent': 94.0,
        'power_ma': 2.0,
        'format': 'int8_quantized',
        'optimized': True
    },
    'speaker_id_original': {
        'name': 'Speaker Identification (Float32)',
        'size_mb': 4.8,
        'latency_ms': 120,
        'accuracy_percent': 96.0,
        'power_ma': 8.0,
        'format': 'float32',
        'optimized': False
    },
    'speaker_id_optimized': {
        'name': 'Speaker Identification (Int8 Quantized)',
        'size_mb': 1.1,
        'latency_ms': 35,
        'accuracy_percent': 95.0,
        'power_ma': 3.0,
        'format': 'int8_quantized',
        'optimized': True
    }
}

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'service': 'model_optimizer'}), 200

@app.route('/models', methods=['GET'])
def list_models():
    """List all available models"""
    try:
        return jsonify({
            'models': MODELS,
            'total_models': len(MODELS),
            'optimized_count': sum(1 for m in MODELS.values() if m.get('optimized'))
        }), 200
    except Exception as e:
        logger.error(f"List models error: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/models/<model_id>', methods=['GET'])
def get_model(model_id):
    """Get details of a specific model"""
    try:
        if model_id not in MODELS:
            return jsonify({'error': 'Model not found'}), 404

        return jsonify({
            'model_id': model_id,
            'details': MODELS[model_id]
        }), 200
    except Exception as e:
        logger.error(f"Get model error: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/compare/<base_model>', methods=['GET'])
def compare_models(base_model):
    """Compare original vs optimized version of a model"""
    try:
        original_key = f'{base_model}_original'
        optimized_key = f'{base_model}_optimized'

        if original_key not in MODELS or optimized_key not in MODELS:
            return jsonify({'error': 'Model pair not found'}), 404

        original = MODELS[original_key]
        optimized = MODELS[optimized_key]

        # Calculate improvements
        size_reduction = 100 * (1 - optimized['size_mb'] / original['size_mb'])
        latency_improvement = 100 * (1 - optimized['latency_ms'] / original['latency_ms'])
        power_reduction = 100 * (1 - optimized['power_ma'] / original['power_ma'])
        accuracy_loss = original['accuracy_percent'] - optimized['accuracy_percent']

        return jsonify({
            'model': base_model,
            'original': original,
            'optimized': optimized,
            'improvements': {
                'size_reduction_percent': round(size_reduction, 1),
                'latency_improvement_percent': round(latency_improvement, 1),
                'power_reduction_percent': round(power_reduction, 1),
                'accuracy_loss_percent': round(accuracy_loss, 1),
                'recommendation': 'DEPLOY' if accuracy_loss <= 2 else 'RETRAIN'
            }
        }), 200
    except Exception as e:
        logger.error(f"Compare models error: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/summary', methods=['GET'])
def optimization_summary():
    """Summary of all optimizations"""
    try:
        total_original = sum(m['size_mb'] for k, m in MODELS.items() if 'original' in k)
        total_optimized = sum(m['size_mb'] for k, m in MODELS.items() if 'optimized' in k)
        total_reduction = 100 * (1 - total_optimized / total_original)

        # Calculate average metrics
        original_models = [m for k, m in MODELS.items() if 'original' in k]
        optimized_models = [m for k, m in MODELS.items() if 'optimized' in k]

        avg_latency_improvement = 100 * (1 -
            sum(m['latency_ms'] for m in optimized_models) /
            sum(m['latency_ms'] for m in original_models))

        avg_accuracy_loss = sum(
            original_models[i]['accuracy_percent'] - optimized_models[i]['accuracy_percent']
            for i in range(len(original_models))
        ) / len(original_models)

        return jsonify({
            'summary': {
                'total_original_size_mb': round(total_original, 2),
                'total_optimized_size_mb': round(total_optimized, 2),
                'total_size_reduction_percent': round(total_reduction, 1),
                'avg_latency_improvement_percent': round(avg_latency_improvement, 1),
                'avg_accuracy_loss_percent': round(avg_accuracy_loss, 1),
                'models_optimized': len(optimized_models),
                'total_models': len(MODELS)
            },
            'recommendation': 'All models optimized successfully. Ready for deployment.',
            'optimization_technique': 'INT8 Post-Training Quantization'
        }), 200
    except Exception as e:
        logger.error(f"Summary error: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/estimate-savings', methods=['POST'])
def estimate_savings():
    """Estimate energy and performance savings"""
    try:
        data = request.json or {}
        daily_activations = data.get('daily_activations', 100)
        device_uptime_hours = data.get('device_uptime_hours', 24)
        baseline_power_ma = data.get('baseline_power_ma', 8)  # Always-listening
        battery_capacity_mah = data.get('battery_capacity_mah', 2000)

        # Power consumption calculation
        # Power = baseline + (inference_power * daily_activations / 3600)
        original_inference_power = 5  # mA per inference
        optimized_inference_power = 2  # mA per inference

        original_total_power = baseline_power_ma + (original_inference_power * daily_activations / 3600)
        optimized_total_power = baseline_power_ma + (optimized_inference_power * daily_activations / 3600)

        daily_ma_hours_original = original_total_power * device_uptime_hours
        daily_ma_hours_optimized = optimized_total_power * device_uptime_hours

        battery_days_original = battery_capacity_mah / daily_ma_hours_original
        battery_days_optimized = battery_capacity_mah / daily_ma_hours_optimized

        return jsonify({
            'assumptions': {
                'daily_wake_word_activations': daily_activations,
                'device_uptime_hours': device_uptime_hours,
                'battery_capacity_mah': battery_capacity_mah,
                'baseline_power_ma': baseline_power_ma
            },
            'power_consumption': {
                'original_avg_power_ma': round(original_total_power, 2),
                'optimized_avg_power_ma': round(optimized_total_power, 2),
                'power_reduction_percent': round(100 * (1 - optimized_total_power / original_total_power), 1),
                'power_saved_ma': round(original_total_power - optimized_total_power, 2)
            },
            'battery_life': {
                'original_days': round(battery_days_original, 1),
                'optimized_days': round(battery_days_optimized, 1),
                'improvement_factor': round(battery_days_optimized / battery_days_original, 1),
                'additional_days': round(battery_days_optimized - battery_days_original, 1)
            }
        }), 200
    except Exception as e:
        logger.error(f"Estimate savings error: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/optimization-manifest', methods=['GET'])
def optimization_manifest():
    """Get the optimization manifest for all models"""
    try:
        manifest = {
            'timestamp': datetime.now().isoformat(),
            'technique': 'INT8 Post-Training Quantization',
            'optimization_status': 'COMPLETE',
            'models': {}
        }

        # Create pairs
        pairs = [
            ('wake_word', 'Wake Word Detection'),
            ('speaker_id', 'Speaker Identification')
        ]

        for pair_name, display_name in pairs:
            original = MODELS.get(f'{pair_name}_original', {})
            optimized = MODELS.get(f'{pair_name}_optimized', {})

            if original and optimized:
                size_reduction = 100 * (1 - optimized['size_mb'] / original['size_mb'])
                latency_improvement = 100 * (1 - optimized['latency_ms'] / original['latency_ms'])
                power_reduction = 100 * (1 - optimized['power_ma'] / original['power_ma'])

                manifest['models'][pair_name] = {
                    'name': display_name,
                    'original_size_mb': original['size_mb'],
                    'optimized_size_mb': optimized['size_mb'],
                    'size_reduction_percent': round(size_reduction, 1),
                    'original_latency_ms': original['latency_ms'],
                    'optimized_latency_ms': optimized['latency_ms'],
                    'latency_improvement_percent': round(latency_improvement, 1),
                    'original_power_ma': original['power_ma'],
                    'optimized_power_ma': optimized['power_ma'],
                    'power_reduction_percent': round(power_reduction, 1),
                    'original_accuracy_percent': original['accuracy_percent'],
                    'optimized_accuracy_percent': optimized['accuracy_percent'],
                    'accuracy_loss_percent': round(
                        original['accuracy_percent'] - optimized['accuracy_percent'], 1)
                }

        return jsonify(manifest), 200
    except Exception as e:
        logger.error(f"Manifest error: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/stats', methods=['GET'])
def get_stats():
    """Get service statistics"""
    try:
        return jsonify({
            'service': 'model_optimizer',
            'optimization_technique': 'INT8 Post-Training Quantization',
            'models_available': len(MODELS),
            'models_optimized': sum(1 for m in MODELS.values() if m.get('optimized')),
            'avg_size_reduction_percent': 76.5,
            'avg_latency_improvement_percent': 67.5,
            'avg_power_reduction_percent': 61.0,
            'avg_accuracy_loss_percent': 1.0,
            'timestamp': datetime.now().isoformat()
        }), 200
    except Exception as e:
        logger.error(f"Stats error: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    logger.info("Model Optimizer Service on port 5110")
    app.run(host='0.0.0.0', port=5110, debug=False)
