# Bug Voice Assistant - Skills Expansion Plan 🚀

**Goal:** Maximize device capabilities with jokes, book reading, storytelling, offline radio, crypto prices, and both screen-based + voice-only games.

**Status:** Research complete, implementation-ready

---

## 📋 Expansion Overview

### Skills to Add (8 Major Features)

1. **Jokes Generator** - Multilingual comedy
2. **Book Reader / Audiobook** - Text-to-audiobook conversion
3. **Story Teller** - AI-generated narratives
4. **Free Online Radio** - Streaming & offline fallback
5. **Crypto Prices** - Real-time & cached data
6. **Screen Games** - Classic arcade (Snake, Tetris, 2048, etc.)
7. **Voice Games** - Trivia, 20 Questions, Word Games
8. **Game Hybrid** - Touch + voice control

---

## 🎭 Feature 1: Jokes Generator

### Research Findings

**Best Approach:**
- Spanish humor corpus (27k tweets) available
- Create local joke database (~500-1000 jokes)
- Support multiple joke categories
- Works 100% offline

**Data Source:**
- [HUMOR: Spanish Humor Corpus](https://pln-fing-udelar.github.io/humor/)
- [Short Jokes Dataset (Kaggle)](https://www.kaggle.com/datasets/abhinavmoudgil95/short-jokes)
- Translate/adapt existing databases

### Implementation Plan

#### Phase 1: Database (1 hour)
```json
{
  "jokes": [
    {
      "id": 1,
      "category": "programming",
      "text": "¿Por qué los programadores prefieren el dark mode? Porque la luz atrae bugs.",
      "language": "es",
      "rating": 4.2
    },
    ...
  ],
  "categories": ["programming", "dad", "animals", "work", "love", "school"],
  "total": 500
}
```

**Tasks:**
- [ ] Create joke_database.json (~500 jokes)
- [ ] Organize by category
- [ ] Add ratings/difficulty
- [ ] ~50 KB file size

#### Phase 2: Service (2 hours)
Create `services/jokes/app.py`:

```python
from flask import Flask, request
import json
import random

app = Flask(__name__)

# Load jokes on startup
with open('jokes_database.json') as f:
    jokes_db = json.load(f)

@app.route('/joke', methods=['GET'])
def get_joke():
    category = request.args.get('category', 'all')
    
    if category == 'all':
        joke = random.choice(jokes_db['jokes'])
    else:
        filtered = [j for j in jokes_db['jokes'] if j['category'] == category]
        joke = random.choice(filtered) if filtered else None
    
    return {'joke': joke['text']}

@app.route('/categories', methods=['GET'])
def get_categories():
    return {'categories': jokes_db['categories']}

@app.route('/health', methods=['GET'])
def health():
    return {'status': 'ok'}
```

**Tasks:**
- [ ] Create Flask service
- [ ] Add random joke endpoint
- [ ] Filter by category
- [ ] Add Docker support

#### Phase 3: Orchestrator Integration (1 hour)
Update orchestrator to handle joke requests:

```python
@app.route('/joke', methods=['POST'])
def handle_joke_request():
    # User says: "Cuéntame un chiste"
    # Orchestrator fetches joke
    # TTS reads it aloud
    
    response = requests.get('http://jokes:5111/joke')
    joke_text = response.json()['joke']
    
    # Send to TTS
    tts_response = requests.post('http://tts:5003/synthesize', 
                                 json={'text': joke_text})
    
    return {'audio': tts_response.json()['audio']}
```

### Complexity: ⭐ LOW
- No external APIs needed
- Small database (~50 KB)
- Simple random selection
- Works 100% offline

---

## 📚 Feature 2: Book Reader / Audiobook Generator

### Research Findings

**Best Approach:**
- Use OfflineTTS (free, no API key)
- Piper TTS (already have piper_es_CO.onnx)
- Support multiple input formats (TXT, EPUB, Markdown)
- Generate audiobook with chapter markers
- Cache audio for replay

**Tools:**
- [OfflineTTS - Free Audiobook Generator](https://offlinetts.com/)
- [Text-to-Speech Studio Offline](https://www.fcportables.com/text-to-speech-studio-offline-portable/)
- Piper TTS (already in project)

### Implementation Plan

#### Phase 1: Book Storage (1 hour)
```
/Volumes/MICROSD/books/
├── uploaded/
│   ├── book1.txt
│   ├── book2.epub
│   └── book3.md
├── processing/
│   └── (temp files)
└── audiobooks/
    ├── book1.mp3
    ├── book1.chapters.json
    └── metadata.json
```

**Tasks:**
- [ ] Define book format support
- [ ] Create storage structure
- [ ] Design metadata format

#### Phase 2: Book Parser Service (2 hours)
Create `services/book_reader/app.py`:

```python
from flask import Flask, request, send_file
import os
import json
from pydub import AudioSegment

app = Flask(__name__)

@app.route('/upload', methods=['POST'])
def upload_book():
    # Accept TXT, EPUB, markdown
    file = request.files['file']
    filename = secure_filename(file.filename)
    
    # Store book
    book_path = f'/sd/books/uploaded/{filename}'
    file.save(book_path)
    
    return {'book_id': filename, 'status': 'uploaded'}

@app.route('/convert/<book_id>', methods=['POST'])
def convert_to_audio(book_id):
    # 1. Parse book
    book_text = read_book(book_id)
    
    # 2. Split into chapters
    chapters = split_chapters(book_text)
    
    # 3. Generate audio for each
    for i, chapter in enumerate(chapters):
        # Use TTS service to convert
        audio = tts_service.synthesize(chapter['text'])
        save_audio(f'/sd/books/audiobooks/{book_id}_ch{i}.mp3', audio)
    
    # 4. Save metadata
    save_metadata(book_id, chapters)
    
    return {'status': 'converted'}

@app.route('/play/<book_id>/<chapter>', methods=['GET'])
def play_chapter(book_id, chapter):
    audio_file = f'/sd/books/audiobooks/{book_id}_ch{chapter}.mp3'
    return send_file(audio_file, mimetype='audio/mpeg')

@app.route('/health', methods=['GET'])
def health():
    return {'status': 'ok'}
```

**Tasks:**
- [ ] Create book parser
- [ ] Support multiple formats
- [ ] Chapter detection
- [ ] TTS integration

#### Phase 3: Integration with TTS (1 hour)
Extend TTS service to handle long-form content:

```python
@app.route('/synthesize/long', methods=['POST'])
def synthesize_long():
    # For books and stories
    text = request.json['text']
    
    # Split by sentences/paragraphs
    chunks = split_text(text, max_len=500)
    
    # Generate audio for each
    audios = []
    for chunk in chunks:
        audio = synthesize(chunk)
        audios.append(audio)
    
    # Concatenate
    final_audio = concatenate_audio(audios)
    
    return {'audio': final_audio}
```

### Complexity: ⭐⭐ MEDIUM
- Requires file handling
- Need to parse EPUB/TXT
- Audio concatenation needed
- Works offline (TTS local)

---

## 📖 Feature 3: Story Teller (AI-Generated Stories)

### Research Findings

**Best Approach:**
- Use Claude/local LLM to generate stories
- Support different genres & lengths
- Cache stories on MicroSD
- Text-to-speech for narration
- Works with or without internet

**Capabilities:**
- Children's stories
- Adventure tales
- Historical narratives
- Custom prompts

### Implementation Plan

#### Phase 1: Story Generator Service (2 hours)
Create `services/story_generator/app.py`:

```python
from flask import Flask, request
import requests
import json

app = Flask(__name__)

STORY_TEMPLATES = {
    'adventure': "Generate a short adventure story in Spanish about a person named {name}...",
    'children': "Generate a funny children's story in Spanish about {topic}...",
    'mystery': "Generate a mystery story in Spanish involving {character}...",
    'fable': "Generate a moral fable in Spanish about {lesson}...",
}

@app.route('/generate', methods=['POST'])
def generate_story():
    data = request.json
    genre = data.get('genre', 'adventure')
    context = data.get('context', 'a brave explorer')
    
    # Create prompt
    template = STORY_TEMPLATES.get(genre)
    prompt = template.format(name=context)
    
    # Get story from LLM
    llm_response = requests.post('http://llm:5002/generate',
                                 json={'prompt': prompt})
    
    story_text = llm_response.json()['response']
    
    # Cache story
    story_id = hash(prompt) % 10000
    save_story(story_id, story_text, genre, context)
    
    return {
        'story_id': story_id,
        'text': story_text,
        'length': len(story_text.split()),
        'genre': genre
    }

@app.route('/story/<story_id>/narrate', methods=['GET'])
def narrate_story(story_id):
    # Get story text
    story = get_story(story_id)
    
    # Convert to audio
    tts_response = requests.post('http://tts:5003/synthesize/long',
                                json={'text': story['text']})
    
    return {'audio': tts_response.json()['audio']}

@app.route('/list', methods=['GET'])
def list_stories():
    return {'stories': get_all_stories()}

@app.route('/health', methods=['GET'])
def health():
    return {'status': 'ok'}
```

**Tasks:**
- [ ] Create story generator service
- [ ] Define story templates
- [ ] Implement caching
- [ ] Add narration support

#### Phase 2: Integration (1 hour)
Add to orchestrator:

```
User: "Cuéntame una historia de aventura"
↓
Story Generator (Claude)
↓
Story Teller (reads aloud via TTS)
↓
Audio response
```

### Complexity: ⭐⭐ MEDIUM
- Requires LLM (Claude or local)
- Text generation
- Audio synthesis
- Caching needed

---

## 📻 Feature 4: Free Online Radio

### Research Findings

**Best Approach:**
- Radio Browser API (62,453+ stations, no key needed)
- Fallback to offline pre-loaded stations
- Stream via WiFi/4G
- Works online only (but cached stations work offline)

**APIs Available:**
- [Radio Browser API](https://radio-browser.info/) - Free, 62K+ stations
- [50K Radio Stations API](https://rapidapi.com/jsonfinder/api/radio/) - 300/month free
- Binance for crypto (free market data)

### Implementation Plan

#### Phase 1: Radio Service (2 hours)
Create `services/radio/app.py`:

```python
from flask import Flask, request, jsonify
import requests
import json

app = Flask(__name__)

RADIO_BROWSER_API = 'https://de1.api.radio-browser.info/json'

# Cached popular stations
POPULAR_STATIONS = [
    {
        'name': 'Radio Caracol Colombia',
        'url': 'https://stream.caracol.com.co/caracol',
        'country': 'CO',
        'language': 'es'
    },
    {
        'name': 'RCN Radio Colombia',
        'url': 'https://stream.rcnradio.com/rcn',
        'country': 'CO',
        'language': 'es'
    },
    {
        'name': 'Blu Radio Colombia',
        'url': 'https://stream.bluradio.com/blu',
        'country': 'CO',
        'language': 'es'
    }
]

@app.route('/search', methods=['GET'])
def search_radio():
    query = request.args.get('q', '')
    country = request.args.get('country', 'CO')
    
    try:
        # Query Radio Browser API
        response = requests.get(
            f'{RADIO_BROWSER_API}/stations/search',
            params={'name': query, 'country': country, 'limit': 10}
        )
        
        stations = response.json()
        return {
            'stations': [
                {'name': s['name'], 'url': s['url'], 'country': s['country']}
                for s in stations
            ]
        }
    except:
        # Fallback to cached stations
        return {'stations': POPULAR_STATIONS}

@app.route('/popular', methods=['GET'])
def get_popular():
    # Return pre-loaded popular stations
    return {'stations': POPULAR_STATIONS}

@app.route('/play/<station_id>', methods=['GET'])
def play_station(station_id):
    # Return stream URL (device handles actual streaming)
    station = get_station(station_id)
    return {'stream_url': station['url']}

@app.route('/countries', methods=['GET'])
def get_countries():
    # List available countries
    return {
        'countries': [
            {'code': 'CO', 'name': 'Colombia'},
            {'code': 'ES', 'name': 'Spain'},
            {'code': 'MX', 'name': 'Mexico'},
            # ... more countries
        ]
    }

@app.route('/health', methods=['GET'])
def health():
    return {'status': 'ok'}
```

**Tasks:**
- [ ] Create radio service
- [ ] Integrate Radio Browser API
- [ ] Cache popular stations
- [ ] Fallback support

#### Phase 2: Device Audio Streaming (1 hour)
Update audio driver to handle streaming:

```c
// In audio_driver.c
void stream_radio(const char *stream_url) {
    // Initialize HTTP client
    http_client_handle_t client = esp_http_client_init(&config);
    
    // Stream audio data to speaker
    while (streaming) {
        int bytes_read = esp_http_client_read(client, buffer, BUFFER_SIZE);
        i2s_write(I2S_NUM_0, buffer, bytes_read, NULL, portMAX_DELAY);
    }
    
    esp_http_client_cleanup(client);
}
```

**Tasks:**
- [ ] HTTP streaming support
- [ ] Buffer management
- [ ] Error handling

### Complexity: ⭐⭐ MEDIUM
- External API integration
- Streaming audio handling
- Fallback to cached stations

---

## 💰 Feature 5: Crypto Prices

### Research Findings

**Best Approach:**
- CoinGecko API (free, no key, 10K calls/month)
- Coinpaprika (keyless, 1K requests/day)
- Cache prices locally (update every 5-10 minutes)
- Works online, shows cached offline

**APIs:**
- [CoinGecko - Demo Tier Free](https://www.coingecko.com/learn/best-free-crypto-api)
- [Coinpaprika Keyless API](https://coinpaprika.com/)
- [CoinMarketCap Basic](https://coinmarketcap.com/)

### Implementation Plan

#### Phase 1: Crypto Service (1.5 hours)
Create `services/crypto/app.py`:

```python
from flask import Flask, request
import requests
import json
import time

app = Flask(__name__)

COINGECKO_API = 'https://api.coingecko.com/api/v3'
CACHE_FILE = '/sd/data/crypto_prices.json'
CACHE_TTL = 300  # 5 minutes

# Popular cryptocurrencies
POPULAR_CRYPTOS = ['bitcoin', 'ethereum', 'cardano', 'ripple', 'solana']

def get_cached_price(crypto):
    try:
        with open(CACHE_FILE) as f:
            cache = json.load(f)
            if cache.get(crypto):
                timestamp = cache[crypto].get('timestamp', 0)
                if time.time() - timestamp < CACHE_TTL:
                    return cache[crypto]['price']
    except:
        pass
    return None

def cache_price(crypto, price):
    try:
        cache = {}
        with open(CACHE_FILE) as f:
            cache = json.load(f)
    except:
        cache = {}
    
    cache[crypto] = {
        'price': price,
        'timestamp': time.time()
    }
    
    with open(CACHE_FILE, 'w') as f:
        json.dump(cache, f)

@app.route('/price/<crypto>', methods=['GET'])
def get_price(crypto):
    currency = request.args.get('currency', 'usd')
    
    # Check cache first
    cached = get_cached_price(crypto)
    if cached:
        return {'price': cached['price'], 'cached': True}
    
    try:
        # Query CoinGecko
        response = requests.get(
            f'{COINGECKO_API}/simple/price',
            params={'ids': crypto, 'vs_currencies': currency}
        )
        
        price = response.json()[crypto][currency]
        
        # Cache it
        cache_price(crypto, price)
        
        return {'price': price, 'cached': False}
    except:
        # Fallback to cache
        cached = get_cached_price(crypto)
        if cached:
            return {'price': cached['price'], 'cached': True, 'old': True}
        return {'error': 'Unable to fetch price'}

@app.route('/popular', methods=['GET'])
def get_popular_prices():
    currency = request.args.get('currency', 'usd')
    
    try:
        response = requests.get(
            f'{COINGECKO_API}/simple/price',
            params={'ids': ','.join(POPULAR_CRYPTOS), 'vs_currencies': currency}
        )
        
        prices = response.json()
        return {'prices': prices}
    except:
        # Return cached prices
        return {'prices': {}, 'cached': True}

@app.route('/health', methods=['GET'])
def health():
    return {'status': 'ok'}
```

**Tasks:**
- [ ] Create crypto service
- [ ] Integrate CoinGecko API
- [ ] Implement caching
- [ ] Add multiple cryptos

#### Phase 2: Integration (1 hour)
Add to orchestrator:

```
User: "¿Cuál es el precio del Bitcoin?"
↓
Crypto Service (check cache, fetch if needed)
↓
TTS reads price
↓
Audio response
```

### Complexity: ⭐ LOW
- Simple API calls
- Local caching
- Works with mock data offline

---

## 🎮 Feature 6: Screen-Based Games

### Research Findings

**Best Games for ESP32-S3 AMOLED:**
- Snake (10-20 KB)
- Tetris (30-40 KB)
- 2048 (20-30 KB)
- Flappy Bird (15-25 KB)
- Space Invaders (30-40 KB)
- Breakout/Brick Breaker (20-30 KB)

**Technology:**
- [MiniGames on GitHub](https://github.com/nturmose21/MiniGames) - Collection of games
- Use existing AMOLED driver
- Touch screen support
- Joystick/button input support

### Implementation Plan

#### Phase 1: Game Framework (2 hours)
Create `firmware/main/games/game_engine.c`:

```c
#include "game_engine.h"

typedef struct {
    int score;
    int level;
    int game_state;  // PLAYING, PAUSED, GAME_OVER
    uint32_t start_time;
} game_context_t;

typedef struct {
    const char *name;
    void (*init)(game_context_t *ctx);
    void (*update)(game_context_t *ctx, input_t input);
    void (*render)(game_context_t *ctx);
    void (*cleanup)(game_context_t *ctx);
} game_t;

void game_loop(game_t *game) {
    game_context_t ctx = {0};
    game->init(&ctx);
    
    while (ctx.game_state != GAME_OVER) {
        // Get input (touch or buttons)
        input_t input = get_input();
        
        // Update game state
        game->update(&ctx, input);
        
        // Render
        game->render(&ctx);
        
        // Small delay
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60 FPS
    }
    
    game->cleanup(&ctx);
}
```

**Tasks:**
- [ ] Create game engine
- [ ] Input handling (touch + buttons)
- [ ] Rendering pipeline
- [ ] Sound effects

#### Phase 2: Individual Games (3 hours total)
Implement each game:

**Snake (1 hour):**
```c
void snake_update(game_context_t *ctx, input_t input) {
    // Move snake based on input
    // Check collisions
    // Update score
    // Check game over
}

void snake_render(game_context_t *ctx) {
    // Clear screen
    // Draw snake
    // Draw food
    // Draw score
}
```

**Tetris (1 hour):**
```c
void tetris_update(game_context_t *ctx, input_t input) {
    // Rotate piece
    // Move left/right
    // Drop piece
    // Clear lines
    // Update score
}
```

**2048 (45 minutes):**
```c
void game2048_update(game_context_t *ctx, input_t input) {
    // Move tiles
    // Merge matching tiles
    // Add new tile
    // Check win condition
}
```

**Tasks:**
- [ ] Implement Snake
- [ ] Implement Tetris
- [ ] Implement 2048
- [ ] Add sound effects

### Complexity: ⭐⭐⭐ HARD
- Requires C/ESP-IDF knowledge
- Graphics rendering
- Input handling
- Frame rate management

---

## 🎤 Feature 7: Voice-Only Games

### Research Findings

**Alexa-Style Games:**
- Akinator (20 Questions)
- Trivia
- Word Association
- Riddles
- Would You Rather
- Hangman (spoken)
- Number Guessing

**Technology:**
- Pure voice interaction
- No screen needed
- LLM-based or rule-based

### Implementation Plan

#### Phase 1: Voice Game Framework (1 hour)
Create `services/voice_games/app.py`:

```python
from flask import Flask, request
import requests
import random
import json

app = Flask(__name__)

class VoiceGame:
    def __init__(self):
        self.state = 'menu'
        self.score = 0
        self.current_question = None
        self.round = 0
    
    def start_game(self, game_type):
        self.game_type = game_type
        self.state = 'playing'
        self.score = 0
        self.round = 0

class Trivia(VoiceGame):
    QUESTIONS = [
        {'q': '¿Cuál es la capital de Francia?', 'a': 'parís'},
        {'q': '¿En qué año cayó el Muro de Berlín?', 'a': '1989'},
        # ... more questions
    ]
    
    def get_question(self):
        self.current_question = random.choice(self.QUESTIONS)
        return self.current_question['q']
    
    def check_answer(self, answer):
        correct = answer.lower() == self.current_question['a'].lower()
        if correct:
            self.score += 10
        return correct

class Akinator(VoiceGame):
    """20 Questions game"""
    
    CHARACTERS = [
        'Batman', 'Superman', 'Spiderman', 'Iron Man',
        'Einstein', 'Shakespeare', 'Cleopatra', 'Napoleon'
    ]
    
    def __init__(self):
        super().__init__()
        self.guessing_character = None
        self.questions_asked = 0
        self.max_questions = 20
    
    def start_thinking(self):
        self.guessing_character = random.choice(self.CHARACTERS)
        return "Think of a character or person. I'll try to guess in 20 questions."
    
    def ask_next_question(self):
        questions = [
            '¿Es una persona real?',
            '¿Está vivo/a?',
            '¿Es un héroe?',
            '¿Es famoso/a?',
            # ... more questions
        ]
        self.questions_asked += 1
        return random.choice(questions)
    
    def make_guess(self):
        return f'¿Estoy pensando en {self.guessing_character}?'

class RiddleGame(VoiceGame):
    """Riddle solver"""
    
    RIDDLES = [
        {
            'riddle': 'Tengo ciudades, pero no casas. Tengo montañas, pero no árboles. ¿Qué soy?',
            'answer': 'un mapa'
        },
        # ... more riddles
    ]
    
    def get_riddle(self):
        riddle = random.choice(self.RIDDLES)
        self.current_riddle = riddle
        return riddle['riddle']
    
    def check_answer(self, answer):
        correct = answer.lower() in self.current_riddle['answer'].lower()
        if correct:
            self.score += 10
        return correct

@app.route('/games', methods=['GET'])
def list_games():
    return {
        'games': [
            'trivia', 'akinator', 'riddles', 'wordgame', 'number_guess'
        ]
    }

@app.route('/play/<game_type>', methods=['POST'])
def start_game(game_type):
    global current_game
    
    if game_type == 'trivia':
        current_game = Trivia()
        current_game.start_game('trivia')
        question = current_game.get_question()
        return {'message': question, 'type': 'question'}
    
    elif game_type == 'akinator':
        current_game = Akinator()
        message = current_game.start_thinking()
        return {'message': message, 'type': 'thinking'}
    
    elif game_type == 'riddles':
        current_game = RiddleGame()
        riddle = current_game.get_riddle()
        return {'message': riddle, 'type': 'riddle'}

@app.route('/answer/<user_answer>', methods=['POST'])
def answer_question(user_answer):
    global current_game
    
    if isinstance(current_game, Trivia):
        is_correct = current_game.check_answer(user_answer)
        if is_correct:
            next_q = "¡Correcto! Siguiente pregunta: " + current_game.get_question()
        else:
            next_q = "Incorrecto. Siguiente pregunta: " + current_game.get_question()
        return {'message': next_q, 'correct': is_correct}
    
    elif isinstance(current_game, RiddleGame):
        is_correct = current_game.check_answer(user_answer)
        if is_correct:
            return {'message': '¡Ganaste! Siguiente acertijo: ' + current_game.get_riddle()}
        else:
            return {'message': 'Intenta de nuevo.'}

@app.route('/guess', methods=['POST'])
def make_guess():
    global current_game
    
    if isinstance(current_game, Akinator):
        guess = current_game.make_guess()
        return {'message': guess, 'type': 'guess'}

@app.route('/score', methods=['GET'])
def get_score():
    return {'score': current_game.score if current_game else 0}

@app.route('/health', methods=['GET'])
def health():
    return {'status': 'ok'}
```

**Tasks:**
- [ ] Create game framework
- [ ] Implement Trivia
- [ ] Implement Akinator (20 Questions)
- [ ] Implement Riddles
- [ ] Implement Word Games
- [ ] Implement Number Guessing

#### Phase 2: Orchestrator Integration (1 hour)
Add voice game handling:

```
User: "Juguemos una trivia"
↓
Voice Games Service (start game)
↓
TTS: "¿Cuál es la capital de Francia?"
↓
User: "París"
↓
Voice Games: Check answer
↓
TTS: "¡Correcto! Siguiente: ..."
```

### Complexity: ⭐⭐ MEDIUM
- LLM integration (for Akinator)
- Question/answer logic
- Score tracking
- No graphics needed

---

## 🎮 Feature 8: Hybrid Games (Touch + Voice)

### Concept
Combine screen games with voice commands:
- "Move left" / "Move right" for Snake
- "Rotate" / "Drop" for Tetris
- "Merge up" for 2048
- Voice hints/help
- Voice feedback (scores, warnings)

### Implementation Plan

#### Phase 1: Input Handler (1 hour)
Extend game engine to accept voice commands:

```c
input_t parse_voice_command(const char *command) {
    input_t input = {0};
    
    if (strstr(command, "left") != NULL) input.left = 1;
    if (strstr(command, "right") != NULL) input.right = 1;
    if (strstr(command, "up") != NULL) input.up = 1;
    if (strstr(command, "down") != NULL) input.down = 1;
    if (strstr(command, "rotate") != NULL) input.button_a = 1;
    if (strstr(command, "pausa") != NULL) input.pause = 1;
    
    return input;
}

void game_loop_hybrid(game_t *game) {
    game_context_t ctx = {0};
    game->init(&ctx);
    
    while (ctx.game_state != GAME_OVER) {
        input_t input = {0};
        
        // Get touch input
        if (touch_detected()) {
            input = get_touch_input();
        }
        
        // Also check for voice input
        if (voice_input_available()) {
            const char *voice_cmd = get_voice_input();
            input_t voice_input = parse_voice_command(voice_cmd);
            // Merge inputs
            input.left |= voice_input.left;
            input.right |= voice_input.right;
            // ... etc
        }
        
        game->update(&ctx, input);
        game->render(&ctx);
        vTaskDelay(pdMS_TO_TICKS(16));
    }
    
    game->cleanup(&ctx);
}
```

**Tasks:**
- [ ] Create voice command parser
- [ ] Map commands to inputs
- [ ] Merge touch + voice inputs

#### Phase 2: Voice Feedback (1 hour)
Add TTS feedback for games:

```c
void announce_game_event(const char *event) {
    // Send to TTS service
    // Announce: "Game Over", "High Score", "Warning", etc.
}

void game_render_with_voice(game_context_t *ctx) {
    // Normal render
    render_graphics(ctx);
    
    // Voice feedback
    if (ctx->line_cleared) {
        announce_game_event("¡Línea completada!");
    }
    
    if (ctx->score_milestone) {
        announce_game_event("¡Felicidades! Puntuación: " + score);
    }
}
```

**Tasks:**
- [ ] Add voice feedback
- [ ] Game announcements
- [ ] Score/progress updates

### Complexity: ⭐⭐ MEDIUM
- Combines voice + screen
- Input merging
- Real-time speech recognition challenges

---

## 📊 Implementation Timeline

### Phase 1: Quick Wins (Week 1-2)
- [ ] Jokes Generator (2 hours)
- [ ] Crypto Prices (2 hours)
- [ ] Voice Games Framework (3 hours)
  - [ ] Trivia
  - [ ] Akinator
  - [ ] Riddles

**Total: ~7 hours**

### Phase 2: Medium Features (Week 3-4)
- [ ] Book Reader Service (3 hours)
- [ ] Story Teller (2 hours)
- [ ] Free Online Radio (2 hours)

**Total: ~7 hours**

### Phase 3: Complex Features (Week 5-6)
- [ ] Screen Games Framework (2 hours)
- [ ] Individual Games (3 hours)
  - [ ] Snake
  - [ ] Tetris
  - [ ] 2048
- [ ] Hybrid Games (2 hours)

**Total: ~7 hours**

**Grand Total: ~21 hours** (spread across 6 weeks)

---

## 🏗️ Architecture Changes

### New Services
```
services/
├── jokes/          (1 service)
├── book_reader/    (1 service)
├── story_generator/ (1 service)
├── radio/          (1 service)
├── crypto/         (1 service)
├── voice_games/    (1 service)
└── [12 existing services]

Total: 18 services
```

### Firmware Changes
```
firmware/main/
├── games/
│   ├── game_engine.c
│   ├── snake.c
│   ├── tetris.c
│   ├── 2048.c
│   └── games.h
├── voice_games/
│   ├── voice_command_parser.c
│   └── voice_games.h
└── [existing modules]
```

### Storage Changes
```
/Volumes/MICROSD/
├── models/          (unchanged)
├── configs/         (unchanged)
├── data/
│   ├── jokes.json         (NEW: 50 KB)
│   ├── crypto_cache.json  (NEW: updated)
│   ├── game_scores.json   (NEW: track scores)
│   ├── radio_stations.json (NEW: cached)
│   └── stories/           (NEW: cached stories)
└── books/                 (NEW: uploaded books + audiobooks)
```

---

## 📈 Expansion Summary

| Feature | Complexity | Time | Offline? | APIs |
|---------|-----------|------|----------|------|
| Jokes | ⭐ | 2h | ✅ | None |
| Book Reader | ⭐⭐ | 3h | ✅ | None |
| Story Teller | ⭐⭐ | 2h | ⚠️ | Claude/LLM |
| Online Radio | ⭐⭐ | 2h | ⚠️ | Radio Browser |
| Crypto Prices | ⭐ | 2h | ⚠️ | CoinGecko |
| Screen Games | ⭐⭐⭐ | 3h | ✅ | None |
| Voice Games | ⭐⭐ | 3h | ✅ | None |
| Hybrid Games | ⭐⭐ | 2h | ✅ | None |
| **Total** | — | **21h** | ✅ | 3 APIs |

---

## 🎯 Recommendations

### Priority Order (Recommended)
1. **Jokes** (easiest, immediate value)
2. **Voice Games** (aligns with device design)
3. **Crypto Prices** (popular, simple)
4. **Book Reader** (resource intensive, worth it)
5. **Screen Games** (best for AMOLED display)
6. **Online Radio** (requires good WiFi)
7. **Story Teller** (CPU intensive)
8. **Hybrid Games** (combines multiple features)

### Resource Management
- **Storage**: All features fit in 128GB easily
  - Jokes: 50 KB
  - Crypto cache: 10 KB
  - Radio cache: 100 KB
  - Game scores: 5 KB
  - Books/audiobooks: Scalable (users choose)
  - **Total: ~165 KB minimum, expandable**

- **Processing**:
  - Voice games: Low CPU (LLM runs on device)
  - Screen games: Medium CPU (graphics rendering)
  - Book reading: Low CPU (TTS handles it)
  - Radio streaming: Network I/O heavy

- **Memory**:
  - Game buffers: 10-20 KB each
  - Audio buffers: 30-50 KB
  - Game state: <5 KB
  - **Total: Well within 520MB ESP32-S3 capacity**

---

## ✅ Next Steps

1. **Choose starting feature** (recommend Jokes)
2. **Create feature branch**
3. **Implement service**
4. **Add to docker-compose.yml**
5. **Integrate with orchestrator**
6. **Test locally**
7. **Merge to main**

---

## 🚀 Conclusion

You can add **8 major features** to maximize device capability:

- ✅ All offline games work 100% offline
- ✅ Voice games are Alexa-competitive
- ✅ Screen games fully utilize AMOLED display
- ✅ Online features (radio, crypto, stories) work with internet
- ✅ All features are cacheable for offline fallback
- ✅ ~21 hours total implementation time

**Device will be a complete entertainment + utility hub!**

---

**Research Complete | Ready to Implement**

