# Bug Voice Assistant - Bilingual English/Spanish Support Plan

**Goal:** Add complete English language support alongside existing Spanish functionality

**Status:** Research complete, implementation-ready

---

## 🌍 Language Support Overview

Bug will become fully bilingual:
- **Spanish (Colombian):** Primary, fully implemented
- **English:** Complete secondary language support

### Automatic Language Detection
- Users speak in either language
- System auto-detects language
- Responds in same language
- No configuration needed

---

## 🎙️ Speech-to-Text (STT) - Bilingual

### Current State
- **Model:** faster-whisper (OpenAI Whisper)
- **Language:** Spanish only (whisper_base.onnx)
- **Accuracy:** ~85-90% for Spanish

### English Support Implementation

#### Option 1: Use Existing whisper_base Model (RECOMMENDED)
```
Whisper base model supports 99 languages including English
- No additional model needed
- Same 150 MB file size
- Accuracy: ~85-90% for English
- Latency: Same as Spanish
- Auto-detection: Built-in
```

**Advantages:**
- ✅ Single model handles both languages
- ✅ No additional storage needed
- ✅ No extra downloads
- ✅ Automatic language detection built-in
- ✅ Already have the model!

**Implementation:**
```python
# In services/stt/app.py
# No changes needed - faster-whisper auto-detects

from faster_whisper import WhisperModel

model = WhisperModel("base")

# This works for both Spanish and English:
segments, info = model.transcribe(audio)
# info.language will be 'es' or 'en' automatically!
```

#### Option 2: English-Specific Model (Alternative)
- **Model:** distil-large-v3 (English-only)
- **Size:** ~50 MB (smaller!)
- **Speed:** 6x faster
- **Accuracy:** Within 1% of base model
- **Tradeoff:** Spanish accuracy reduces slightly

**Not recommended** - use Option 1 (multilingual base)

### Language Detection Integration
```python
# Detect language from transcription
language = info.language  # 'en' or 'es'

# Pass to orchestrator
send_to_orchestrator({
    'text': transcribed_text,
    'language': language
})
```

---

## 🔊 Text-to-Speech (TTS) - Bilingual

### Current State
- **Model:** piper_es_CO.onnx (Colombian Spanish)
- **Voice:** Salome Neural (female)
- **Language:** Spanish only

### English Support Implementation

#### Option 1: Add English Piper Voice (RECOMMENDED)
```
Piper TTS Models for English:
├─ piper_en_US_lessac
│  ├─ Size: 40 MB
│  ├─ Quality: High (natural, clear)
│  ├─ Speed: Real-time
│  └─ Gender: Female
├─ piper_en_US_ljspeech
│  ├─ Size: 50 MB
│  ├─ Quality: Very High (professional)
│  ├─ Speed: Real-time
│  └─ Gender: Female
└─ piper_en_US_ryan
   ├─ Size: 45 MB
   ├─ Quality: High
   ├─ Speed: Real-time
   └─ Gender: Male
```

**Storage Analysis:**
```
Current:
├─ piper_es_CO.onnx        50 MB
└─ whisper_base.onnx       150 MB
Total: 200 MB

With English:
├─ piper_es_CO.onnx        50 MB
├─ piper_en_US_lessac.onnx  40 MB (ADD THIS)
└─ whisper_base.onnx       150 MB
Total: 240 MB

Cost: 40 MB additional (128GB available)
Recommendation: Use piper_en_US_lessac (balance quality/speed)
```

**Implementation:**
```python
# In services/tts/app.py
from piper.voice import PiperVoice

voices = {
    'es': PiperVoice.load('piper_es_CO.onnx'),
    'en': PiperVoice.load('piper_en_US_lessac.onnx')
}

@app.route('/synthesize', methods=['POST'])
def synthesize():
    text = request.json['text']
    language = request.json.get('language', 'es')
    
    voice = voices[language]
    audio = voice.synthesize(text)
    
    return {'audio': audio}
```

#### Option 2: Alternative English TTS (Advanced)
- **Kokoro:** Better quality, natural voices
- **KittenTTS:** Lightweight
- **F5-TTS:** Voice cloning

**Not recommended initially** - stick with Piper for consistency

---

## 🧠 Language Model (LLM) - Multilingual

### Current State
- **Primary:** Claude API (multilingual, supports both)
- **Fallback:** Local TinyLLaMA (limited multilingual)

### English Support Implementation

#### Claude API (Already Works)
Claude natively supports both English and Spanish:

```python
# Current implementation works for both
prompt_es = "¿Cuál es la capital de Francia?"
prompt_en = "What is the capital of France?"

# Both work with Claude API - no changes needed!
response = anthropic.messages.create(
    model="claude-opus",
    messages=[{"role": "user", "content": prompt_en}]
)
```

**Status:** ✅ Already functional

#### Local Fallback - TinyLLaMA Enhancement
```
Current: TinyLaMA-1.1B (limited multilingual)

Options for English support:
1. Keep as-is: Works but English quality lower
2. Use Qwen-1.8B: Better multilingual (3x larger)
3. Use Phi-4-mini: Good multilingual (3.8B params)
4. Fine-tune TinyLLaMA: Best option (resource-heavy)
```

**Recommendation:** Keep TinyLLaMA (works for basic English)

---

## 🌐 Orchestrator - Language Routing

### Automatic Language Detection & Response

```python
# Updated orchestrator.py

from langdetect import detect_langs
import flask

# Load language detection
def detect_language(text):
    try:
        # Use lightweight langdetect
        lang_prob = detect_langs(text)
        language = lang_prob[0].lang
        return language if language in ['en', 'es'] else 'es'
    except:
        return 'es'  # Default to Spanish

@app.route('/voice', methods=['POST'])
def process_voice():
    audio_data = request.json['audio']
    
    # 1. STT (auto-detects language)
    stt_response = requests.post('http://stt:5001/transcribe',
                                json={'audio': audio_data})
    text = stt_response.json()['text']
    language = stt_response.json()['language']  # 'en' or 'es'
    
    # 2. LLM (language-aware response)
    llm_response = requests.post('http://llm:5002/generate',
                                json={'prompt': text,
                                      'language': language})
    response_text = llm_response.json()['response']
    
    # 3. TTS (matches language)
    tts_response = requests.post('http://tts:5003/synthesize',
                                json={'text': response_text,
                                      'language': language})
    audio = tts_response.json()['audio']
    
    return {'audio': audio, 'text': response_text, 'language': language}
```

**No user configuration needed** - fully automatic!

---

## 📚 Feature Content - Bilingual Expansion

### 1. Jokes Generator - Both Languages

```json
{
  "jokes": [
    {
      "id": 1,
      "category": "programming",
      "text_es": "¿Por qué los programadores prefieren dark mode? Porque la luz atrae bugs.",
      "text_en": "Why do programmers prefer dark mode? Because light attracts bugs.",
      "language": ["es", "en"],
      "rating": 4.2
    }
  ]
}
```

**Storage:** 
- Spanish jokes: 50 KB
- English jokes: Add 80 KB (more English jokes available)
- **Total:** 130 KB

**Data Sources:**
- Spanish: [HUMOR Corpus](https://pln-fing-udelar.github.io/humor/)
- English: [r/Jokes Dataset (550k jokes)](https://github.com/orionw/rJokesData)

### 2. Voice Games - Both Languages

```python
class Trivia:
    QUESTIONS = {
        'es': [
            {'q': '¿Cuál es la capital de Francia?', 'a': 'París'}
        ],
        'en': [
            {'q': 'What is the capital of France?', 'a': 'Paris'}
        ]
    }
    
    def get_question(self, language='es'):
        return random.choice(self.QUESTIONS[language])
```

**Content Strategy:**
- Trivia: Translate questions or use language-specific
- Akinator: Pure logic (works in any language)
- Riddles: Translate existing riddles
- Word games: Create English versions

### 3. All Other Features

**Book Reader:**
- Supports both languages (TTS handles it)
- Auto-detects book language

**Story Teller:**
- Claude API: Generates in any language
- Genre templates for both languages

**Online Radio:**
- Spanish stations: Colombian, Mexican, Spanish
- English stations: US, UK, international

**Crypto Prices:**
- Works in any language
- Currency: USD, EUR, COP (Colombian Peso)
- Voice: "Bitcoin is $X" or "Bitcoin es $X"

**Screen Games:**
- UI: Bilingual menus
- Scores: Language-agnostic

**Hybrid Games:**
- Voice commands: Both languages
- Feedback: Responds in user's language

---

## 🔧 Implementation Steps

### Phase 1: Core Language Support (2-3 hours)

1. **Download English Piper TTS Model** (1 hour)
   ```bash
   # Get English TTS
   # piper_en_US_lessac.onnx (40 MB)
   # Place in: microsd_files/models/
   ```

2. **Update TTS Service** (30 minutes)
   ```python
   # Add English voice loading
   # Route based on language parameter
   ```

3. **Test STT Auto-detection** (30 minutes)
   ```bash
   # Verify whisper_base auto-detects 'en' vs 'es'
   # Test both languages
   ```

4. **Update Orchestrator** (1 hour)
   ```python
   # Add language detection
   # Route responses by language
   # Test end-to-end flows
   ```

### Phase 2: Feature Content (3-4 hours)

5. **English Jokes Database** (1 hour)
   ```bash
   # Download from GitHub (550k jokes)
   # Convert to JSON
   # Organize by category
   ```

6. **Bilingual Voice Games** (2 hours)
   ```python
   # Translate/create English versions
   # Trivia questions
   # Riddle database
   # Word association
   ```

7. **Update All Services** (1 hour)
   ```python
   # Add language parameter support
   # Update prompts for both languages
   ```

### Phase 3: Testing & Polish (2 hours)

8. **Comprehensive Testing**
   - Test both languages for all features
   - Verify auto-detection accuracy
   - Check edge cases (code-switching, mixed language)

9. **User Configuration** (Optional)
   - Add language preference setting
   - Allow override of auto-detection

---

## 📊 Bilingual Resource Requirements

### Storage
```
Current (Spanish only):
├─ whisper_base.onnx          150 MB
├─ piper_es_CO.onnx            50 MB
└─ Jokes database              50 KB
Total: ~200 MB

With English Support:
├─ whisper_base.onnx          150 MB (unchanged)
├─ piper_es_CO.onnx            50 MB (unchanged)
├─ piper_en_US_lessac.onnx     40 MB (NEW)
├─ English jokes              80 KB (NEW)
├─ Bilingual game content      50 KB (NEW)
└─ Other configs              <5 MB
Total: ~240 MB

Available: 128 GB
Used: 0.19%
Remaining: 99.81% 🎉
```

### Processing (No Change)
- TTS models same size/speed
- STT same (already multilingual)
- LLM: Claude works in both languages

### Memory (No Change)
- Models load same way
- Language detection lightweight (<1 MB)

---

## 🎯 User Experience - Seamless Bilingual

### Scenario 1: Spanish User
```
User (Spanish): "¿Qué hora es?"
↓
STT (auto-detect): Spanish
↓
LLM: Responds in Spanish
↓
TTS: Speaks Spanish
↓
Device: "Son las 3 en punto"
```

### Scenario 2: English User
```
User (English): "What time is it?"
↓
STT (auto-detect): English
↓
LLM: Responds in English
↓
TTS: Speaks English
↓
Device: "It's 3 o'clock"
```

### Scenario 3: Family (Mixed Languages)
```
User A (Spanish): "Cuéntame un chiste"
Device: Spanish joke
↓
User B (English): "Tell me a joke"
Device: English joke
↓
User C (Spanish): "Juguemos trivia"
Device: Spanish trivia
(All without any setup!)
```

---

## ✅ Feature Parity Checklist

| Feature | Spanish | English | Notes |
|---------|---------|---------|-------|
| STT | ✅ | ✅ | Same model |
| TTS | ✅ | ✅ (new) | Add piper_en_US_lessac |
| LLM | ✅ | ✅ | Claude API native |
| Q&A | ✅ | ✅ | Language-agnostic |
| Jokes | ✅ | ✅ (new) | Add English jokes DB |
| Trivia | ✅ | ✅ (new) | Translate questions |
| Akinator | ✅ | ✅ (new) | Pure logic |
| Riddles | ✅ | ✅ (new) | Translate riddles |
| Games | ✅ | ✅ (new) | Bilingual UI |
| Radio | ✅ | ✅ (new) | Add English stations |
| Crypto | ✅ | ✅ | Language-agnostic |
| Books | ✅ | ✅ | Auto-detect language |
| Stories | ✅ | ✅ | Claude supports both |

**Result:** Full feature parity in both languages

---

## 🚀 Implementation Plan

### Timeline
```
PHASE 1: Core (2-3 hours)
├─ Download English TTS       1h
├─ Update services           1h
└─ Test integration          1h

PHASE 2: Content (3-4 hours)
├─ English jokes database    1h
├─ Game content (bilingual)  2h
└─ Service updates           1h

PHASE 3: Polish (2 hours)
├─ Testing                   1.5h
└─ Edge cases               0.5h

TOTAL: ~7-9 hours
```

### Files to Modify
```
services/
├─ stt/app.py               (add language output)
├─ tts/app.py               (add English voice)
├─ llm/app.py               (language-aware prompts)
├─ orchestrator/app.py      (language routing)
├─ jokes/app.py             (bilingual jokes DB)
├─ voice_games/app.py       (English questions)
└─ other services           (language parameters)

firmware/
├─ No changes needed        (language agnostic)

microsd_files/
├─ models/
│  └─ piper_en_US_lessac.onnx (ADD)
└─ jokes_database.json      (UPDATE: add English)
```

---

## 🌟 Key Advantages

✅ **Automatic:** No user setup needed  
✅ **Seamless:** Same device, both languages  
✅ **Efficient:** Minimal storage (40 MB)  
✅ **Complete:** All features bilingual  
✅ **Natural:** Each user speaks their language  
✅ **Family-friendly:** Everyone can use Bug  
✅ **Professional:** Production-ready implementation  

---

## 📝 Language Detection Library

**Recommended:** langdetect (lightweight, fast)

```python
# Installation
pip install langdetect

# Usage
from langdetect import detect, detect_langs

language = detect("Hello world")  # 'en'
language = detect("Hola mundo")   # 'es'

# With probability
langs = detect_langs("What is this?")
# [en:0.9999...]
```

**Alternatives:**
- [Lingua-py](https://github.com/pemistahl/lingua-py) - More accurate
- [FastText](https://fasttext.cc/) - Faster for high-volume
- [Polyglot](https://polyglot.readthedocs.io/) - 100+ languages

---

## 🎉 Conclusion

Bug becomes a **true bilingual assistant** with minimal effort:

✅ **40 MB additional storage**  
✅ **~7-9 hours implementation**  
✅ **Full feature parity**  
✅ **Automatic language detection**  
✅ **No user configuration**  

Users can speak Spanish or English, and Bug responds in the same language automatically!

**Next Steps:**
1. Download piper_en_US_lessac.onnx
2. Update 6-7 service files
3. Add English jokes database
4. Test both languages

🚀 Ready to implement bilingual support!

