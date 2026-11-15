from flask import Flask, jsonify, request
from datetime import datetime
import requests

app = Flask(__name__)

# Mock smart home device database
DEVICES = {
    "light_living_room": {
        "id": "light_lr",
        "name": "Luz sala",
        "type": "light",
        "location": "living_room",
        "state": "off",
        "brightness": 0,
        "supported_commands": ["on", "off", "brightness"]
    },
    "light_bedroom": {
        "id": "light_br",
        "name": "Luz dormitorio",
        "type": "light",
        "location": "bedroom",
        "state": "off",
        "brightness": 0,
        "supported_commands": ["on", "off", "brightness"]
    },
    "thermostat": {
        "id": "thermo_1",
        "name": "Termostato",
        "type": "thermostat",
        "location": "main",
        "state": "on",
        "temperature": 22,
        "target_temperature": 22,
        "supported_commands": ["on", "off", "set_temperature"]
    },
    "door_lock": {
        "id": "lock_1",
        "name": "Ceradura puerta",
        "type": "lock",
        "location": "entrance",
        "state": "locked",
        "supported_commands": ["lock", "unlock"]
    },
    "speaker": {
        "id": "speaker_1",
        "name": "Parlante inteligente",
        "type": "speaker",
        "location": "living_room",
        "state": "idle",
        "volume": 50,
        "supported_commands": ["volume", "pause", "play"]
    }
}

# Automation rules
AUTOMATIONS = [
    {
        "id": "auto_1",
        "name": "Buenas noches",
        "trigger": "sunset",
        "actions": [
            {"device": "light_living_room", "command": "brightness", "value": 30},
            {"device": "thermostat", "command": "set_temperature", "value": 18}
        ],
        "enabled": True
    },
    {
        "id": "auto_2",
        "name": "Salir de casa",
        "trigger": "voice_command",
        "actions": [
            {"device": "light_living_room", "command": "off"},
            {"device": "light_bedroom", "command": "off"},
            {"device": "door_lock", "command": "lock"}
        ],
        "enabled": True
    }
]

@app.route('/health', methods=['GET'])
def health():
    return jsonify({"status": "healthy", "service": "smart_home"}), 200

@app.route('/devices', methods=['GET'])
def list_devices():
    """List all smart home devices"""
    try:
        devices_list = [
            {
                "id": dev_id,
                "name": device["name"],
                "type": device["type"],
                "location": device["location"],
                "state": device["state"]
            }
            for dev_id, device in DEVICES.items()
        ]

        return jsonify({
            "devices": devices_list,
            "total": len(devices_list),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/device/<device_id>', methods=['GET'])
def get_device(device_id):
    """Get specific device details"""
    try:
        if device_id not in DEVICES:
            return jsonify({"error": "Device not found"}), 404

        device = DEVICES[device_id]
        return jsonify({
            "id": device_id,
            **device,
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/device/<device_id>/command', methods=['POST'])
def send_command(device_id):
    """Send command to device"""
    try:
        if device_id not in DEVICES:
            return jsonify({"error": "Device not found"}), 404

        data = request.get_json() or {}
        command = data.get("command")
        value = data.get("value")

        device = DEVICES[device_id]

        if command not in device["supported_commands"]:
            return jsonify({
                "error": f"Command '{command}' not supported",
                "supported": device["supported_commands"]
            }), 400

        # Execute command
        if device["type"] == "light":
            if command == "on":
                device["state"] = "on"
                device["brightness"] = 100
            elif command == "off":
                device["state"] = "off"
                device["brightness"] = 0
            elif command == "brightness":
                device["brightness"] = value if 0 <= value <= 100 else 50
                device["state"] = "on" if value > 0 else "off"

        elif device["type"] == "thermostat":
            if command == "on":
                device["state"] = "on"
            elif command == "off":
                device["state"] = "off"
            elif command == "set_temperature":
                device["target_temperature"] = value if 15 <= value <= 30 else 22

        elif device["type"] == "lock":
            if command == "lock":
                device["state"] = "locked"
            elif command == "unlock":
                device["state"] = "unlocked"

        elif device["type"] == "speaker":
            if command == "volume":
                device["volume"] = value if 0 <= value <= 100 else 50
            elif command == "pause":
                device["state"] = "paused"
            elif command == "play":
                device["state"] = "playing"

        return jsonify({
            "success": True,
            "device": device_id,
            "command": command,
            "new_state": device.get("state"),
            "message": f"Comando '{command}' ejecutado en {device['name']}",
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/automations', methods=['GET'])
def list_automations():
    """List automation rules"""
    try:
        return jsonify({
            "automations": AUTOMATIONS,
            "total": len(AUTOMATIONS),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/automation/<automation_id>/trigger', methods=['POST'])
def trigger_automation(automation_id):
    """Trigger an automation rule"""
    try:
        automation = None
        for auto in AUTOMATIONS:
            if auto["id"] == automation_id:
                automation = auto
                break

        if not automation:
            return jsonify({"error": "Automation not found"}), 404

        if not automation["enabled"]:
            return jsonify({"error": "Automation is disabled"}), 400

        # Execute all actions in automation
        results = []
        for action in automation["actions"]:
            device_id = action["device"]
            command = action["command"]
            value = action.get("value")

            if device_id in DEVICES:
                device = DEVICES[device_id]
                # Execute command logic here (same as /device/<device_id>/command)
                results.append({
                    "device": device_id,
                    "command": command,
                    "status": "executed"
                })

        return jsonify({
            "automation": automation["name"],
            "actions_executed": len(results),
            "results": results,
            "message": f"Automatización '{automation['name']}' ejecutada",
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/scenes', methods=['GET'])
def list_scenes():
    """Get predefined scenes (good morning, good night, etc)"""
    try:
        scenes = {
            "good_morning": {
                "name": "Buenos días",
                "description": "Enciende luces, abre cortinas, inicia noticias",
                "actions": [
                    {"device": "light_living_room", "command": "brightness", "value": 100},
                    {"device": "light_bedroom", "command": "brightness", "value": 80}
                ]
            },
            "good_night": {
                "name": "Buenas noches",
                "description": "Apaga luces, baja temperatura, asegura puertas",
                "actions": [
                    {"device": "light_living_room", "command": "off"},
                    {"device": "light_bedroom", "command": "off"},
                    {"device": "thermostat", "command": "set_temperature", "value": 18},
                    {"device": "door_lock", "command": "lock"}
                ]
            },
            "movie_time": {
                "name": "Película",
                "description": "Apaga luces, baja volumen, cierra cortinas",
                "actions": [
                    {"device": "light_living_room", "command": "brightness", "value": 10},
                    {"device": "speaker", "command": "volume", "value": 30}
                ]
            }
        }

        return jsonify({
            "scenes": scenes,
            "total": len(scenes),
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/scene/<scene_name>/activate', methods=['POST'])
def activate_scene(scene_name):
    """Activate a scene"""
    try:
        scenes = {
            "good_morning": [
                {"device": "light_living_room", "brightness": 100},
                {"device": "light_bedroom", "brightness": 80}
            ],
            "good_night": [
                {"device": "light_living_room", "state": "off"},
                {"device": "light_bedroom", "state": "off"},
                {"device": "door_lock", "state": "locked"}
            ]
        }

        if scene_name not in scenes:
            return jsonify({"error": "Scene not found"}), 404

        # Execute scene
        scene_name_display = {
            "good_morning": "Buenos días",
            "good_night": "Buenas noches",
            "movie_time": "Película"
        }.get(scene_name, scene_name)

        return jsonify({
            "scene": scene_name_display,
            "status": "activated",
            "message": f"Escena '{scene_name_display}' activada",
            "timestamp": datetime.now().isoformat()
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == '__main__':
    print("🏠 Smart Home Integration Service Starting...")
    print(f"📱 {len(DEVICES)} devices configured")
    print(f"⚙️ {len(AUTOMATIONS)} automations available")
    print("Supported device types: light, thermostat, lock, speaker")
    app.run(host='0.0.0.0', port=5118, debug=False)
