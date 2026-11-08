# Where each feature runs

The rule, set by the owner: **the board does everything it can by itself. What it cannot do it asks a free online
service for, called by the board over Wi-Fi. The PC is the last resort**, because the board must keep working for long
periods with no PC and, often, no internet.

| Level | Meaning |
|---|---|
| **Board** | Works with no PC and no internet |
| **Online** | The board itself calls a free internet service (no PC needed) |
| **PC** | Needs the PC server (only when nothing above can do it) |

## Hearing and speaking

| Feature | Now | Target | Notes |
|---|---|---|---|
| Wake word "Hola Bug" | **Board** (done: the sentence recogniser has a "wake" class; the PC is only a backup) | **Board** (small model trained from the owner's own voice, guided recording) | Needs the guided training (below) |
| Understanding commands | **Board** for 28 fixed phrases (done, about 60 to 70% right with 5 recordings each; more recordings improve it); PC (Whisper) as a backup | **Board** for a fixed set of about 30 phrases; **Online** (free speech-to-text, needs a free account) for free speech; PC last | A chip this size cannot do free Spanish speech-to-text |
| Speaking answers | Board (recorded words) for local skills; PC (Edge/Piper) for the rest | **Board** for everything made of known words and numbers; **Online** (free text-to-speech) for other text; PC last | |
| Screen, touch, dimming, battery notices | Board | Board | |
| Clock | Board (clock chip + internet time) | Board | |

## Skills

| Skill | Now | Target | Needs |
|---|---|---|---|
| Time, date | Board | Board | |
| Timers, alarms, reminders (no label), repeats, cancel, list | Board | Board | Exact alarm times by voice need the PC or online speech-to-text; the board offers presets and, later, a time picker on screen |
| Stopwatch, volume, stop music, battery, home | Board | Board | |
| Tic-tac-toe, trivia | Board (tic-tac-toe); PC (trivia) | Board | Trivia questions are text: store them on the board, ask by voice with recorded clips |
| Shopping list, notes | Board | Board | The items are shown, not read aloud, unless the board has a text-to-speech |
| Next alarm on Home, error notices, screen power saving | Board | Board | |
| Holidays (Colombia) | **Board** (done) | **Board** | Computed, no data needed; needs recorded day, month and number words (already there) |
| Weather, sunrise and sunset | PC | **Online** (Open-Meteo, free, no key) | Spoken with recorded numbers and condition words; city from the internet connection |
| Dollar, euro, bitcoin, ethereum prices | **Online** (done: open.er-api.com and CoinGecko, called by the board) | **Online** | Spoken with recorded numbers |
| News headlines | **Online** (built, untested: Google News RSS read by the board, spoken with the online voice) | **Online** | Shown on screen; spoken only with an online text-to-speech |
| Wikipedia, jokes | **Online** (built, untested: Wikipedia summaries; jokes from JokeAPI translated by MyMemory, 12 stored ones as a fallback; spoken with Google's online voice) | **Online** | Spoken with an online text-to-speech; PC last |
| Math (+ − × ÷), unit conversions | **Board** (done: 12 units, numbers up to 999 million spoken with recorded words) | **Board** | Spoken with recorded numbers |
| Translation | **Online** (built, untested: MyMemory translates and Google's voice speaks it in that language) | **Online** | Spoken with an online text-to-speech |
| Radio | **Online** (built, to be tested: the board searches Radio Browser and decodes MP3 streams itself; PC relay stays as backup for AAC stations) | **Online** | Station search with the free Radio Browser directory |
| Music from files | **Board** (built, to be tested: MP3 files in /sd/music) | **Board**: MP3 files on the board's microSD card | Needs the MP3 decoder; the card is optional hardware |
| Radio favourites | **Board** (built, untested: five stations kept in the board's memory) | **Board** | |
| Books | **Board** (built, tested on the PC: .txt files in /sd/books, a page at a time, the place is remembered; spoken with the online voice) | **Board** | |
| Morning briefing | **Board** (built, tested on the PC: date, weather and today's agenda, spoken with the online voice) | **Board** + online weather | |
| Calendar (ICS links) | **Online** (built and tested with a sample calendar: the board downloads the ICS file itself, handles repeats; titles are shown, not spoken) | **Online** | Repeating events are the hard part |
| Email (IMAP) | **Online** (built, not yet tested with a real mailbox: the board lists the newest messages and the unread count; reading a message aloud is also built: the board decodes the message text and speaks it with the online voice; answering and deleting stay with the PC) | **Online** | Needs TLS and some memory; Gmail app password |
| Firmware updates | Online (already) | Online | |
| Free conversation, stories, summaries | not built | Online (needs a key) | No key available |

## The order of work

1. **Voice on the board**: guided recording of the owner's voice, then a wake-word model and a command model that run on
   the board. Until they are reliable, the PC keeps helping with hearing.
2. **Online services called by the board**: weather, prices, holidays and countdowns, math (some of them need nothing online).
3. **Radio and music on the board**: MP3 decoding, microSD card.
4. **Calendar and email directly from the board.**
5. **A free online text-to-speech** for text the board has no recording of (news, Wikipedia, books).
6. **The PC becomes optional**: it keeps its skills only as a last fallback and for the first-time training of models.

## What stays on the PC on purpose

Training the voice models (needs a GPU and PyTorch). Making the recorded words and the test data. Everything else on
the PC is a fallback.
