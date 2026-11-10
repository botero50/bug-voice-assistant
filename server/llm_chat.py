"""Free conversation fallback: when no skill understands a sentence, a small model running on this PC (Ollama) answers.
The board stays first (its own skills), the online services second, and this PC model is the last resort.
If Ollama is not running or is slow, the caller keeps its old "No te entendí." answer."""
import json
import logging
import os
import re
import threading
import urllib.request

log = logging.getLogger("llm_chat")

URL = os.environ.get("BUG_LLM_URL", "http://127.0.0.1:11434/api/chat")
MODEL = os.environ.get("BUG_LLM_MODEL", "qwen3:8b")
TIMEOUT = float(os.environ.get("BUG_LLM_TIMEOUT", "25"))
ENABLED = os.environ.get("BUG_LLM", "1") != "0"
MEMORY = 3  # exchanges remembered for follow-ups

SYSTEM = (
    "Eres Bug, un asistente de voz pequeño que habla español colombiano, cálido y directo. "
    "Tus respuestas se leen en voz alta: máximo dos o tres frases cortas, sin listas, sin markdown, sin emojis, sin asteriscos. "
    "No tienes acceso a internet ni a la hora actual: si te piden datos de hoy (clima, noticias, precios, hora), di que para eso usen esas preguntas directamente. "
    "Si no sabes algo, dilo con sinceridad en una frase. No inventes datos ni enlaces."
)

_history = []
_lock = threading.Lock()


def clean(text):
    text = re.sub(r"<think>.*?</think>", "", text, flags=re.S)
    text = re.sub(r"[*_`#>\[\]]+", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text[:420]


def ask(sentence):
    """The model's short answer, or None (disabled, Ollama away, too slow, empty)."""
    if not ENABLED or not sentence.strip():
        return None
    with _lock:
        messages = [{"role": "system", "content": SYSTEM}]
        for question, answer in _history[-MEMORY:]:
            messages += [{"role": "user", "content": question}, {"role": "assistant", "content": answer}]
        messages.append({"role": "user", "content": sentence})
    body = json.dumps({"model": MODEL, "messages": messages, "stream": False, "think": False, "keep_alive": "20m",
                       "options": {"num_predict": 160, "temperature": 0.6}}).encode()
    try:
        request = urllib.request.Request(URL, data=body, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=TIMEOUT) as response:
            answer = clean(json.load(response).get("message", {}).get("content", ""))
    except Exception as error:
        log.warning("The PC model did not answer (%s)", error)
        return None
    if not answer:
        return None
    with _lock:
        _history.append((sentence, answer))
        del _history[:-MEMORY]
    return answer


def warm_up():
    """Loads the model into the GPU in the background so the first free question does not wait for it."""
    def load():
        try:
            body = json.dumps({"model": MODEL, "messages": [{"role": "user", "content": "hola"}], "stream": False, "think": False,
                               "keep_alive": "20m", "options": {"num_predict": 1}}).encode()
            urllib.request.urlopen(urllib.request.Request(URL, data=body, headers={"Content-Type": "application/json"}), timeout=120).read()
            log.info("The PC model %s is loaded", MODEL)
        except Exception as error:
            log.info("The PC model is not available (%s): free questions will say 'No te entendí'", error)
    if ENABLED:
        threading.Thread(target=load, daemon=True).start()
