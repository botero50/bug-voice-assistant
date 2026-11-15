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

POWER_PROFILES = {
    'always_listening': {
        'name': 'ALWAYS_LISTENING',
        'listen_ms': 0,
        'sleep_ms': 0,
        'power_ma': 8.0,
        'latency_ms': 50,
        'battery_days': 8,
        'description': 'Always on, <100ms wake latency'
    },
    'periodic_listening': {
        'name': 'PERIODIC_LISTENING',
        'listen_ms': 500,
        'sleep_ms': 2000,
        'power_ma': 2.0,
        'latency_ms': 2000,
        'battery_days': 30,
        'description': 'Listen 500ms, sleep 2s, 4x power reduction'
    },
    'gesture_only': {
        'name': 'GESTURE_ONLY',
        'listen_ms': 0,
        'sleep_ms': 0,
        'power_ma': 0.5,
        'latency_ms': 0,
        'battery_days': 60,
        'description': 'Button/tap activation only'
    }
}

simulation_state = {
    'current_profile': 'always_listening',
    'start_time': datetime.now(),
    'cycles_completed': 0,
    'total_listen_time_ms': 0,
    'total_sleep_time_ms': 0,
    'estimated_battery_drain': 0.0
}

@app.route('/health', methods=['GET'])
def health():
    return jsonify({'status': 'healthy', 'service': 'power_optimizer'}), 200

@app.route('/profiles', methods=['GET'])
def get_profiles():
    return jsonify(POWER_PROFILES), 200

@app.route('/profile/current', methods=['GET'])
def get_current_profile():
    profile_key = simulation_state['current_profile']
    profile = POWER_PROFILES[profile_key]
    return jsonify({
        'profile': profile_key,
        'details': profile,
        'battery_days_remaining': profile['battery_days']
    }), 200

@app.route('/profile/set', methods=['POST'])
def set_profile():
    data = request.json
    profile_key = data.get('profile', 'always_listening')

    if profile_key not in POWER_PROFILES:
        return jsonify({'error': 'Invalid profile'}), 400

    old_profile = simulation_state['current_profile']
    simulation_state['current_profile'] = profile_key
    profile = POWER_PROFILES[profile_key]

    logger.info(f"Profile switched: {old_profile} → {profile_key}")
    logger.info(f"  Power: {profile['power_ma']}mA, Latency: {profile['latency_ms']}ms")

    return jsonify({
        'success': True,
        'old_profile': old_profile,
        'new_profile': profile_key,
        'estimated_power_ma': profile['power_ma'],
        'battery_life_days': profile['battery_days']
    }), 200

@app.route('/simulate/cycle', methods=['POST'])
def simulate_cycle():
    """Simulate one listen/sleep cycle"""
    data = request.json or {}
    profile_key = simulation_state['current_profile']
    profile = POWER_PROFILES[profile_key]

    listen_time = profile['listen_ms']
    sleep_time = profile['sleep_ms']
    total_cycle_time = listen_time + sleep_time

    simulation_state['cycles_completed'] += 1
    simulation_state['total_listen_time_ms'] += listen_time
    simulation_state['total_sleep_time_ms'] += sleep_time

    # Simple power calculation
    if total_cycle_time > 0:
        power_per_ms = profile['power_ma'] / 1000.0
        cycle_power = power_per_ms * total_cycle_time
    else:
        cycle_power = profile['power_ma']

    simulation_state['estimated_battery_drain'] += cycle_power

    return jsonify({
        'cycle': simulation_state['cycles_completed'],
        'listen_ms': listen_time,
        'sleep_ms': sleep_time,
        'power_consumed_ma_ms': cycle_power,
        'total_cycles': simulation_state['cycles_completed'],
        'profile': profile_key
    }), 200

@app.route('/simulate/battery', methods=['GET'])
def get_battery_simulation():
    """Estimate battery life for current profile"""
    profile_key = simulation_state['current_profile']
    profile = POWER_PROFILES[profile_key]

    # Assume 2000mAh battery, 30min daily usage
    battery_capacity_mah = 2000
    daily_usage_minutes = 30
    total_daily_ma_minutes = profile['power_ma'] * daily_usage_minutes
    days_per_charge = battery_capacity_mah / total_daily_ma_minutes if total_daily_ma_minutes > 0 else 999

    return jsonify({
        'profile': profile_key,
        'battery_capacity_mah': battery_capacity_mah,
        'daily_usage_minutes': daily_usage_minutes,
        'power_consumption_ma': profile['power_ma'],
        'estimated_days_per_charge': round(days_per_charge, 1),
        'improvement_vs_always_listening': round(
            POWER_PROFILES['always_listening']['battery_days'] / profile['battery_days'], 1
        )
    }), 200

@app.route('/switch-analysis', methods=['POST'])
def analyze_switch():
    """Analyze impact of switching between profiles"""
    data = request.json
    from_profile = data.get('from', 'always_listening')
    to_profile = data.get('to', 'periodic_listening')

    if from_profile not in POWER_PROFILES or to_profile not in POWER_PROFILES:
        return jsonify({'error': 'Invalid profile'}), 400

    from_data = POWER_PROFILES[from_profile]
    to_data = POWER_PROFILES[to_profile]

    power_reduction = ((from_data['power_ma'] - to_data['power_ma']) / from_data['power_ma']) * 100
    battery_improvement = to_data['battery_days'] / from_data['battery_days']

    return jsonify({
        'from': from_profile,
        'to': to_profile,
        'power_reduction_percent': round(power_reduction, 1),
        'battery_improvement_factor': round(battery_improvement, 1),
        'power_before_ma': from_data['power_ma'],
        'power_after_ma': to_data['power_ma'],
        'battery_before_days': from_data['battery_days'],
        'battery_after_days': to_data['battery_days'],
        'latency_before_ms': from_data['latency_ms'],
        'latency_after_ms': to_data['latency_ms']
    }), 200

@app.route('/stats', methods=['GET'])
def get_stats():
    profile_key = simulation_state['current_profile']
    profile = POWER_PROFILES[profile_key]

    return jsonify({
        'service': 'power_optimizer',
        'current_profile': profile_key,
        'profile_details': profile,
        'simulation': {
            'cycles_completed': simulation_state['cycles_completed'],
            'total_listen_ms': simulation_state['total_listen_time_ms'],
            'total_sleep_ms': simulation_state['total_sleep_time_ms'],
            'estimated_drain': round(simulation_state['estimated_battery_drain'], 2)
        },
        'available_profiles': list(POWER_PROFILES.keys())
    }), 200

if __name__ == '__main__':
    logger.info("Power Optimizer Service on port 5108")
    app.run(host='0.0.0.0', port=5108, debug=False)
