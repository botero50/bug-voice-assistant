"""Starts or stops a recording session for a clean TEST set of "Hola Bug" (see docs/IMPROVEMENT_PLAN.md, Phase 1).

    python wake/start_session.py            start: 100 clips, one "Hola Bug" per prompt, a different condition each time
    python wake/start_session.py 60         start with another number
    python wake/start_session.py --raw      50 greetings in the HARD conditions (whisper, music, TV, sideways), and the board also sends each
                                            microphone separately (stereo 32 kHz, server/wake_data/raw) for wake/audio_lab.py
    python wake/start_session.py --status   how many are recorded
    python wake/start_session.py --stop     end the session (the board goes back to the normal list of phrases)

While it runs, say "Hola Bug, entrena mi voz": the board asks for one greeting at a time, like "Hola Bug  (a tres metros)". The clips are
saved with the time in their name. Measure them BEFORE retraining:  python wake/baseline.py --after <the "started" value printed here>
and train with  BUG_HOLDOUT_AFTER=<the same value>  so the new clips are never trained on.
"""
import json
import sys
from datetime import datetime
from pathlib import Path

SERVER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SERVER))
import train_api  # noqa: E402


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    if arg == "--stop":
        train_api.SESSION_FILE.unlink(missing_ok=True)
        print("Session ended: the board asks for the normal phrases again.")
        return
    if arg == "--status":
        session = train_api._session()
        if not session:
            sys.exit("No session is running.")
        print(f"{train_api._session_count(session)} of {session['target']} recorded (started {session['started']})")
        return
    raw = "--raw" in sys.argv
    numbers = [a for a in sys.argv[1:] if a.isdigit()]
    target = int(numbers[0]) if numbers else (50 if raw else 100)
    session = {"id": "wake", "target": target, "started": datetime.now().strftime("%Y%m%d_%H%M%S"), "raw": raw}
    train_api.SESSION_FILE.write_text(json.dumps(session), encoding="utf-8")
    print(f"Session started: {target} greetings. Started: {session['started']}")
    print('Say "Hola Bug, entrena mi voz" to the board and follow the prompts.')


if __name__ == "__main__":
    main()
