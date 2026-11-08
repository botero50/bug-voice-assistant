# The gaming area

Say **"juguemos"** (or "juegos", "minijuegos") and the board opens the games menu. Say **"juguemos memoria"**, **"2048"**,
**"mayor o menor"**, **"topos"**, **"simón"** or **"triqui"** to go straight to one. Every game has a **Salir** button at the bottom; the
menu closes with a swipe up. Records are kept in the board's memory and shown on the menu.

## What exists today

The menu lists every game by category (Fuerza, Velocidad, Defensa, Mente, Diversión, Arcade) and scrolls. Records are kept in the
board's memory. Say "juguemos" for the menu, or the name of a game ("juguemos memoria", "pon el laberinto", "baile", "2048"...).

| Category | Game | How it plays |
|---|---|---|
| Fuerza | **Topos** | tap the green circles for 25 s, avoid the red X; three lives |
| | **Globos** | pop the rising balloons in 30 s |
| | **Canica** | tilt the board (or put a finger where the marble should roll); collect berries, avoid the holes; 60 s, three lives |
| | **Laberinto** | swipe or tilt to move through a new 7x7 maze to the green square; fewer moves is better |
| Velocidad | **Reacción** | tap as soon as the box turns green; 5 rounds, the average in ms (lower is better) |
| | **Toques** | tap or shake as many times as you can in 10 s |
| | **Lluvia** | five lanes: berries fall, rocks cost a life; tap a lane to move the basket |
| | **Cuerda** | tap to jump as the rope reaches the ground; it speeds up; three lives |
| | **Vuelo** | tap to flap between the tubes |
| Defensa | **Atrapa** | drag a basket to catch the red fruit and dodge the dark bombs |
| | **Equilibrio** | tilt the board (or hold left or right) to keep the ball on a bar; seconds survived |
| | **Ladrillos** | drag or tilt the paddle, break the 20 bricks; three lives |
| | **Memoria** | find the 8 pairs; fewer tries is better |
| | **Simón dice** | repeat a growing tune of four coloured pads |
| Mente | **Figuras** | name the silhouette before the bar runs out; three lives |
| | **Preguntas** | 10 general-knowledge questions with 3 answers (20 in the bank) |
| | **Puzle** | slide the tiles into order 1 to 8; always solvable |
| | **Mayor o menor** | cards 1 to 9: will the next be higher or lower? three lives |
| Diversión | **Izquierda o derecha** | guess which way it turns; three lives |
| | **Baile** | 48 notes fall in four columns; tap on the frame (±90 ms = 2 points, ±180 ms = 1); five lives |
| | **Escondite** | follow the ball under three cups as they swap (3 to 11 swaps, faster each round) |
| | **Corredor** | tap to jump the obstacles for 90 s; three lives |
| Arcade | **2048** | swipe any way; equal numbers join into the next |
| | **Burbujas** | tap groups of two or more of one colour |
| | **Dibujo** | draw with a finger, pick a colour, clear |
| | **Triqui** (tic-tac-toe) | built earlier: tap a square; the board plays O and slips now and then |
| Voice | **Trivia** | "juguemos trivia", "la B"; 30 questions, on the PC |

Everything above builds, flashes and starts, and each game was opened on the board for a few seconds without a crash. **None was played
yet**, so rules, speed and layout are untested by a person. Merge's rules have host tests (below).

## Documented in this project but not built

From `docs/FEATURES.md` and `README.md`: **Akinator** (20 questions; a PC service), **riddles** and **word games** by voice.

## From TamaPoke (https://github.com/botero50/TamaPoke)

TamaPoke runs on the **same board** and has 28 minigames in six categories. Its code is MIT licensed, so its rules can be reused with
the credit in `CREDITS.md`. Its sprites and Pokémon data are not copied: the games here are drawn with plain shapes, and the same names
and categories are kept.

| TamaPoke game | Here |
|---|---|
| Mole Pop, Memory, Simon says, Higher / Lower, Merge | Topos, Memoria, Simón dice, Mayor o menor, 2048 (Merge's rules are TamaPoke's) |
| Left / Right, Balloon pop, Slide puzzle, Bubbles, Drawing | Izquierda o derecha, Globos, Puzle, Burbujas, Dibujo |
| Berry Drop, Catch | Lluvia, Atrapa |
| Jump Rope, Dance, Hide & Seek, Flappy flight, Brick Bash | Cuerda, Baile, Escondite, Vuelo, Ladrillos |
| Marble Roll, Tilt maze, Balance | Canica, Laberinto, Equilibrio (tilt the board, or use a finger or swipes) |
| Reaction test, Shake race | Reacción, Toques (tap or shake) |
| Eevee Run (secret) | Corredor (a generic runner, not secret) |
| Who is it?, Type quiz | Figuras (silhouettes of icons), Preguntas (general knowledge) |
| Ball game, bag (the pet's original trainers) | Not for Bug: they need the pet |

The pet's economy (tickets, prizes, badges, stats) does not exist in Bug, so games only keep a record.

### Tilting and shaking

The board's motion sensor (QMI8658) has a driver (`firmware/main/imu.c`) that is read only while a tilt game is open. The pose the board is in
when a game starts counts as level, so it works on the stand or in the hand. Tilting works in **Canica** (the marble rolls), **Equilibrio**
(the bar tilts), **Atrapa** and **Ladrillos** (the basket and the paddle slide) and **Laberinto** (one step per tilt); **Toques** counts
shakes as taps. On this board the sensor's X axis runs up the screen and Y across it (found by tilting it on its stand), so tilting the right edge down is "right" and the bottom edge down is "down". A finger on the screen always wins over the tilt. The sensor was checked on the board at rest (about 1 g, steady); the
tilting games themselves have not been played.

## Test cases from TamaPoke

TamaPoke has about 60 test files in `tools/emu/tests`, run on a PC emulator of the board. Most are about the pet, battles, saves and the
installer. The ones about games are `arcade_test.cpp` and `games46_test.cpp`; they and the rules (`arcade.h`) are kept in
`docs/reference/tamapoke/`. The Merge cases were rewritten for C in `server/skills_test/games_test.c` (13 checks, all pass) and run against
`firmware/main/games_rules.h`, the same file the board uses. The other cases (ball bouncing on a round wall, marble roll, the runner) are
ready to be ported when those games are built.

Run them with:

```bash
cd server/skills_test && gcc games_test.c -o games_test.exe && ./games_test.exe
```
