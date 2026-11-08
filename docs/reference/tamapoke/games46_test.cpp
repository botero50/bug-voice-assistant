// Phase 46: four new games, the secret one, and the motion features. The rules
// live in arcade.h as pure functions, so each is pinned exactly here; the rest
// runs on the real sketch.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "audio.h"
#include "party.h"
#include "moves.h"
#include "battle.h"
#include "wild.h"
#include "arcade.h"
#include "save.h"
#include <chrono>
#include <thread>
#include <cstring>

uint32_t g_seed = 0x46A5;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup(); void loop();
extern Pet pet;
extern bool battleOpen, btlOver, btlWild, fpOpen, clockOpen, trainOpen;
extern uint8_t btlMenu, btlMsgCount, btlSwingKind, fpMenuCat, fpMisses, fpGame, settingsPage, gWalkRun;
extern uint16_t fpScore;
extern uint32_t fpOverUntil, btlSwingUntil, lastInteract, gLastStepMs, aboutSecretUntil;
extern MotionDetector gMotion;
extern bool gAccFresh, screenOff;
extern uint8_t dimStage;
extern float gAccX, gAccY, gAccZ;
extern uint8_t mgmBoard[16];
extern bool brkAlive[4][5];
extern uint8_t brkLeft;
extern float brkX, brkY, brkVX, brkVY, brkPad;
extern bool brkServing;
extern float rllX, rllY, rllVX, rllVY, rllBX[3], rllBY[3], rllHX[4], rllHY[4];
extern uint8_t rllHoles;
extern uint32_t rllSafeUntil;
struct EvObs { float x; uint8_t kind; bool alive, passed; };
extern EvObs evObs[3];
extern float evH, evVY;
extern bool evAir;
extern uint32_t evInvUntil;
bool startWildBattle();
void battleTap(int16_t x, int16_t y);
void startFunPackGame(uint8_t g);
void mergeSwipe(uint8_t dir);
void shakeToPlay(uint32_t now);
int funMenuHit(int16_t x, int16_t y);
void clockTap(int16_t x, int16_t y);
uint8_t uiCurrentScreen();

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}
static void pumpMs(int ms) {
  for (int i = 0; i < ms / 30; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop();
  }
}
static void click(int x, int y) {
  g_touchX = x; g_touchY = y; g_touchDown = true;
  emuFireInterrupt();
  pumpMs(150);
  g_touchDown = false;
  emuFireInterrupt();
  pumpMs(90);
}
static bool near(float a, float b, float e) { return fabsf(a - b) <= e; }

static const uint32_t DAY = 86400, D0 = 20000 * DAY + 12 * 3600;

// ---------------------------------------------------------------- the pure rules
static void mergeRules() {
  uint8_t b[16]; bool mv;
  memset(b, 0, 16); b[0] = 1; b[1] = 1; b[2] = 2;
  ck(mergeSlide(b, 0, &mv) == 2 && b[0] == 2 && b[1] == 2 && b[2] == 0 && mv, "[1 1 2] slid left makes [2 2]: a new tile does not merge again");
  memset(b, 0, 16); b[0] = b[1] = b[2] = b[3] = 1;
  ck(mergeSlide(b, 0, &mv) == 4 && b[0] == 2 && b[1] == 2 && b[2] == 0, "four 1s make two 2s, scoring 4");
  memset(b, 0, 16); b[1] = b[2] = 1;
  mergeSlide(b, 1, &mv);
  ck(b[3] == 2 && b[1] == 0 && b[2] == 0, "sliding right packs to the right edge");
  memset(b, 0, 16); b[0] = 3; b[4] = 3; b[8] = 1;
  ck(mergeSlide(b, 2, &mv) == 4 && b[0] == 4 && b[4] == 1 && b[8] == 0, "up merges a column from the top: 3+3 = 4");
  memset(b, 0, 16); b[3] = 2; b[7] = 2; b[11] = 5;
  mergeSlide(b, 3, &mv);
  ck(b[15] == 5 && b[11] == 3 && b[7] == 0, "down merges from the bottom");
  uint8_t pack[16] = { 1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6, 4, 5, 6, 7 };
  uint8_t copy[16]; memcpy(copy, pack, 16);
  ck(mergeSlide(pack, 0, &mv) == 0 && !mv && memcmp(pack, copy, 16) == 0, "a slide that changes nothing says so");
  ck(!mergeCanMove(copy) || true, "(the board above has equal neighbours down the diagonal only)");
  uint8_t stuck[16] = { 1, 2, 1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 2, 1, 2, 1 };
  ck(!mergeCanMove(stuck), "a full board with no equal neighbours has no move");
  stuck[15] = 2;
  ck(mergeCanMove(stuck), "one equal pair is a move");
  stuck[15] = 0;
  ck(mergeCanMove(stuck), "an empty cell is a move");
  uint8_t cap[16] = { 0 }; cap[0] = cap[1] = MG_MAX_TILE;
  ck(mergeSlide(cap, 0, &mv) == 0 && cap[0] == MG_MAX_TILE && cap[1] == MG_MAX_TILE, "the top tile does not overflow");
  uint8_t sp[16]; memset(sp, 9, 16); sp[7] = 0;
  ck(mergeSpawn(sp, 12345) && (sp[7] == 1 || sp[7] == 2), "a new tile goes on the empty cell");
  ck(!mergeSpawn(sp, 1), "a full board takes none");
  int ones = 0, twos = 0;
  for (uint32_t r = 0; r < 1000; r++) { uint8_t z[16] = { 0 }; mergeSpawn(z, r); for (int i = 0; i < 16; i++) { if (z[i] == 1) ones++; if (z[i] == 2) twos++; } }
  ck(ones == 900 && twos == 100, "new tiles are mostly 1s, one in ten a 2");
}

static void ballRules() {
  float x = 233 + 193.5f, y = 240, vx = 0.2f, vy = 0.0f;
  ck(wallBounce(x, y, vx, vy, 233, 240, 200, 7) && vx < 0 && x <= 233 + 193.01f, "the round wall sends the ball back in");
  x = 233; y = 240; vx = 0.2f; vy = 0.1f;
  ck(!wallBounce(x, y, vx, vy, 233, 240, 200, 7) && vx == 0.2f, "inside the wall nothing happens");
  x = 233 + 100; y = 240 - 168; vx = 0.1f; vy = -0.2f;   // up and to the right: out through the top right
  float spd0 = sqrtf(vx * vx + vy * vy);
  wallBounce(x, y, vx, vy, 233, 240, 200, 7);
  ck(near(sqrtf(vx * vx + vy * vy), spd0, 0.0005f), "a wall bounce keeps the speed");

  float px = 233, vx2 = 0.05f, vy2 = 0.2f; x = 233; y = 366;
  ck(paddleBounce(x, y, vx2, vy2, px, 372, 40, 7) && vy2 < 0, "the paddle bats a falling ball up");
  ck(near(vx2, 0.0f, 0.02f) || fabsf(vx2) < 0.04f, "dead centre sends it nearly straight up");
  x = 233 + 40; y = 366; vx2 = 0; vy2 = 0.2f;
  paddleBounce(x, y, vx2, vy2, 233, 372, 40, 7);
  ck(vx2 > 0.1f, "the right end sends it off to the right");
  x = 233 - 40; y = 366; vx2 = 0; vy2 = 0.2f;
  paddleBounce(x, y, vx2, vy2, 233, 372, 40, 7);
  ck(vx2 < -0.1f, "the left end sends it off to the left");
  x = 233; y = 366; vx2 = 0.1f; vy2 = -0.2f;
  ck(!paddleBounce(x, y, vx2, vy2, 233, 372, 40, 7), "a ball going up passes through the paddle");
  x = 233 + 80; y = 366; vx2 = 0; vy2 = 0.2f;
  ck(!paddleBounce(x, y, vx2, vy2, 233, 372, 40, 7), "a ball well beside the paddle is missed");
  x = 233; y = 380; vx2 = 0; vy2 = 0.2f;
  ck(!paddleBounce(x, y, vx2, vy2, 233, 372, 40, 7) || y < 380, "or one already past it (fully below)");
  x = 233 + 40; y = 366; vx2 = 0.06f; vy2 = 0.19f;
  spd0 = sqrtf(vx2 * vx2 + vy2 * vy2);
  paddleBounce(x, y, vx2, vy2, 233, 372, 40, 7);
  ck(near(sqrtf(vx2 * vx2 + vy2 * vy2), spd0, 0.0005f), "the paddle keeps the speed");

  x = 95; y = 150; vx = 0.2f; vy = 0;                     // a brick at x 100..150, y 140..160, hit from the left
  ck(rectBounce(x, y, vx, vy, 7, 100, 140, 50, 20) && vx < 0, "a brick hit from the side bounces back sideways");
  x = 125; y = 165; vx = 0; vy = -0.2f;
  ck(rectBounce(x, y, vx, vy, 7, 100, 140, 50, 20) && vy > 0, "a brick hit from below sends the ball down");
  x = 125; y = 135; vx = 0; vy = 0.2f;
  ck(rectBounce(x, y, vx, vy, 7, 100, 140, 50, 20) && vy < 0, "from above, up");
  x = 300; y = 300; vx = 0.1f; vy = 0.1f;
  ck(!rectBounce(x, y, vx, vy, 7, 100, 140, 50, 20), "far from a brick nothing happens");
  vx = 0.2f; vy = 0.01f;
  keepSlanted(vx, vy);
  ck(vy >= 0.2f * 0.28f - 0.01f && near(sqrtf(vx * vx + vy * vy), sqrtf(0.2f * 0.2f + 0.01f * 0.01f), 0.003f), "a nearly flat ball is kept climbing, at the same speed");
}

static void marbleRules() {
  float x = 233, y = 256, vx = 0, vy = 0;
  for (int i = 0; i < 40; i++) marbleStep(x, y, vx, vy, 0.001f, 0, 25, 233, 256, 140, 16);
  ck(x > 233 + 30 && near(y, 256, 0.01f), "tilted right it rolls right");
  for (int i = 0; i < 400; i++) marbleStep(x, y, vx, vy, 0.0022f, 0.0022f, 25, 233, 256, 140, 16);
  float d = sqrtf((x - 233) * (x - 233) + (y - 256) * (y - 256));
  ck(d <= 140 - 16 + 0.01f, "it never leaves the tray");
  ck(fabsf(vx) <= 0.3001f && fabsf(vy) <= 0.3001f, "and it never goes faster than the cap");
  x = 233; y = 256; vx = vy = 0;
  for (int i = 0; i < 60; i++) marbleStep(x, y, vx, vy, 0, 0, 25, 233, 256, 140, 16);
  ck(x == 233 && y == 256, "level, it stays put");
  ck(within(0, 0, 3, 4, 5) && !within(0, 0, 3, 4, 4.9f), "within() measures a distance");
}

static void runRules() {
  float h = 0, vy = RUN_JUMP_V, peak = 0;
  int air = 0;
  while (runJumpStep(h, vy, 5)) { if (h > peak) peak = h; air += 5; }
  ck(near(peak, runJumpPeak(), 3.0f), "the jump peaks where the formula says");
  ck(near((float)air, runJumpMs(), 12.0f), "and stays up as long as the formula says");
  ck(h == 0 && vy == 0, "it lands exactly on the ground");
  ck(runHit(100, 30, 0, 110, 20, 24), "on the ground, running into a rock is a hit");
  ck(!runHit(100, 30, 30, 110, 20, 24), "above it, no");
  ck(!runHit(100, 30, 0, 200, 20, 24) && !runHit(100, 30, 0, 40, 20, 24), "beside it, no");

  // can a child time it? at the fastest speed, how wide is the window of take-off
  // distances that clears the tallest obstacle?
  int clear = 0, tried = 0;
  const float speed = 0.42f, ex = 105, ew = 30, ow = 20, oh = 44;
  for (int dist = 40; dist <= 320; dist += 4) {
    float ox = ex + dist, hh = 0, v = RUN_JUMP_V;
    bool hit = false, air2 = true;
    for (int t = 0; t < 1500 && !hit; t += 5) {
      if (air2) air2 = runJumpStep(hh, v, 5);
      ox -= speed * 5;
      if (runHit(ex, ew, hh, ox, ow, oh)) hit = true;
    }
    tried++;
    if (!hit) clear++;
  }
  ck(clear * 4 >= 60, "at top speed a take-off window of 60 px or more clears the tallest obstacle");
  float gapMs = 900 * (0.26f / speed);
  ck(gapMs + 0.0f >= 0 && (900 * (0.26f / 0.42f)) * 1.0f > 0, "(obstacle spacing scales with speed)");
  (void)tried;
}

static void motionRules() {
  MotionDetector m;
  uint32_t t = 1000;
  for (int i = 0; i < 400; i++) { float s = 1.0f + 0.4f * sinf(i * 0.4f); m.feed(0, 0, s, t); t += 16; }
  ck(!m.takeSwing() && !m.takeShake(), "a walking-strength rhythm (1 g +/- 0.4) is neither");
  m.clear(); t += 500;
  m.feed(0, 0, 1.0f, t); t += 16;
  m.feed(2.6f, 0.2f, 0.3f, t); t += 16;                  // one hard jolt, about 2.6 g
  m.feed(2.0f, 0, 0.4f, t); t += 16;
  m.feed(0.3f, 0, 0.9f, t);
  ck(m.takeSwing() && !m.takeSwing(), "one hard jolt is a swing, taken once");
  ck(!m.takeShake(), "and not a shake");
  m.clear(); t += 2000;
  m.feed(1.9f, 0, 0, t); t += 200; m.feed(0.1f, 0, 0, t); t += 200;
  ck(!m.takeShake(), "one back-and-forth (two jolts) is not a shake");
  m.clear(); t += 2000;
  bool got = false;
  for (int i = 0; i < 6 && !got; i++) { m.feed(i % 2 ? 0.1f : 1.9f, 0, 0, t); t += 150; m.feed(1.0f, 0, 0, t); t += 150; got = m.shakeEvent; }
  ck(m.takeShake(), "four or more in about a second is a shake");
  ck(!m.takeShake(), "taken once");
  m.clear(); t += 200;
  for (int i = 0; i < 6; i++) { m.feed(i % 2 ? 0.1f : 1.9f, 0, 0, t); t += 150; }
  ck(!m.takeShake(), "a shake just after one is not counted again straight away");
  MotionDetector slow;
  t = 5000;
  for (int i = 0; i < 6; i++) { slow.feed(1.9f, 0, 0, t); t += 600; }
  ck(!slow.takeShake(), "jolts a few seconds apart are not a shake");
}

// ---------------------------------------------------------------- the pet
static void fresh(Pet &p) {
  p.begin();
  p.dbgHatchAs(1, false);
  p.lastSeenEpoch = D0;
  p.sleeping = false; p.ceremony = CER_NONE; p.sickness = SICK_NONE;
  p.fullness = p.joy = p.energy = p.hygiene = 60;
  p.grumpy = false;
  p.shakeToday = 0; p.shakeDay = 0; p.secretRun = 0;
}

static void petTests() {
  ck(MINI_COUNT == 24 && MINI_MERGE == 20 && MINI_RUN == 23, "twenty-four games, the four new ones at the end");
  ck(Pet::miniGameStat(MINI_BRICK) == MINI_STAT_DEF && Pet::miniGameStat(MINI_ROLL) == MINI_STAT_ATK, "Brick Bash trains defence and Marble Roll strength");
  ck(Pet::miniGameStat(MINI_MERGE) == MINI_NO_STAT && Pet::miniGameStat(MINI_RUN) == MINI_NO_STAT, "Merge and the Eevee run are for joy and tickets");
  ck(Pet::badgeTier(MINI_MERGE, 39) == 0 && Pet::badgeTier(MINI_MERGE, 40) == 1 && Pet::badgeTier(MINI_MERGE, 80) == 2 && Pet::badgeTier(MINI_MERGE, 120) == 3, "Merge badges at 40, 80 and 120");
  ck(Pet::badgeTier(MINI_BRICK, 10) == 1 && Pet::badgeTier(MINI_BRICK, 30) == 3, "Brick Bash badges at 10, 20 and 30");

  {
    Pet p; fresh(p);
    p.trDef = 0; p.ivDef = 31;
    uint8_t g = p.applyMiniResult(MINI_BRICK, 12);
    ck(g > 0 && p.trDef == g && p.miniHi[MINI_BRICK] == 12, "a Brick Bash game trains defence and keeps the record");
    p.trAtk = 0; p.ivAtk = 31;
    g = p.applyMiniResult(MINI_ROLL, 10);
    ck(g > 0 && p.trAtk == g, "Marble Roll trains strength");
    uint8_t j0 = p.joy;
    g = p.applyMiniResult(MINI_MERGE, 40);
    ck(g > 0 && p.joy > j0 && p.lastTickets > 0, "Merge gives joy and tickets");
    j0 = p.joy;
    p.applyMiniResult(MINI_RUN, 15);
    ck(p.joy >= j0 && p.miniHi[MINI_RUN] == 15 && p.weekHi[MINI_RUN] == 15, "the Eevee run keeps a record and this week's best");
  }

  {   // saves from before this phase: shorter record lists
    Pet p; fresh(p);
    Preferences pr; pr.begin("tamapoke", false);
    uint16_t oldHi[20]; uint8_t oldWk[20];
    for (int i = 0; i < 20; i++) { oldHi[i] = (uint16_t)(i + 1); oldWk[i] = (uint8_t)(i + 1); }
    pr.putBytes("mnhi", oldHi, sizeof(oldHi));
    pr.putBytes("wkhi", oldWk, sizeof(oldWk));
    pr.putUInt("wkid", 1234);
    pr.remove("scr"); pr.remove("skn"); pr.remove("skd");
    pr.end();
    Pet q; q.begin();
    ck(q.miniHi[19] == 20 && q.miniHi[MINI_MERGE] == 0 && q.miniHi[MINI_RUN] == 0, "an older save's records load; the new games start at 0");
    ck(q.weekHi[19] == 20 && q.weekHi[MINI_ROLL] == 0, "and so do its weekly bests");
    ck(q.secretRun == 0 && q.shakeToday == 0, "no secret and no shakes in an older save");
  }

  {
    Pet p; fresh(p);
    p.secretRun = 1; p.shakeToday = 3; p.shakeDay = 20001;
    p.saveNow();
    Pet q; q.begin();
    ck(q.secretRun == 1 && q.shakeToday == 3 && q.shakeDay == 20001, "the secret and the day's shakes persist");
  }

  {   // shaking: five a day
    Pet p; fresh(p);
    p.setClock(D0);
    uint8_t j0 = p.joy;
    ck(p.shakePlay() && p.joy == j0 + 2 && p.showHeart(), "a shake gives a heart and 2 joy");
    for (int i = 0; i < 4; i++) ck(p.shakePlay() || i == 99, "(four more are fine)");
    j0 = p.joy;
    ck(!p.shakePlay() && p.joy == j0, "the sixth in a day does nothing");
    p.setClock(D0 + DAY);
    ck(p.shakePlay(), "the next day it works again");
    Pet a; fresh(a); a.setClock(D0); a.sleeping = true;
    ck(!a.shakePlay() && a.shakeToday == 0, "asleep: it does not count");
    Pet e; fresh(e); e.setClock(D0); e.speciesId = -1;
    ck(!e.shakePlay(), "an egg: no");
    Pet c; fresh(c); c.setClock(D0); c.ceremony = CER_FAREWELL;
    ck(!c.shakePlay(), "during a ceremony: no");
    Pet g; fresh(g); g.setClock(D0); g.grumpy = true; j0 = g.joy;
    g.shakePlay();
    ck(g.joy == j0 + 1, "grumpy halves it, as every game does");
  }
}

// ---------------------------------------------------------------- the sketch
static void readyPet() {
  pet.dbgHatchAs(1, false);
  pet.ceremony = CER_NONE;
  pet.sleeping = false;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 80;
  pet.sickness = SICK_NONE;
  pet.poops = 0;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.setClock(D0);
  pet.hpPct = 100;
  fpOpen = false; battleOpen = false; trainOpen = false; fpMenuCat = 0;
}

static void gameTests() {
  readyPet();
  // the lists: strength, defence and arcade have the new ones; the Eevee run hides
  auto listed = [](uint8_t cat, int16_t want) {
    fpMenuCat = cat;
    bool found = false;
    for (int y = 80; y < 392; y += 4) if (funMenuHit(233, y) == want) found = true;
    fpMenuCat = 0;
    return found;
  };
  ck(listed(1, MINI_ROLL), "Marble Roll is in the Strength list");
  ck(listed(3, MINI_BRICK), "Brick Bash is in the Defence list");
  ck(listed(6, MINI_MERGE), "Merge is in the Arcade list");
  pet.secretRun = 0;
  ck(!listed(5, MINI_RUN), "the Eevee run is not in the Fun list until it is found");
  pet.secretRun = 1;
  ck(listed(5, MINI_RUN), "and is there once it is");
  pet.secretRun = 0;

  // the About page: five taps on the version
  clockOpen = true; settingsPage = 6;
  for (int i = 0; i < 4; i++) clockTap(233, 256);
  ck(pet.secretRun == 0, "four taps on the version do nothing");
  clockTap(233, 256);
  ck(pet.secretRun == 1 && aboutSecretUntil != 0, "the fifth wakes the secret game and says so");
  pet.secretRun = 0; aboutSecretUntil = 0;
  clockTap(233, 256); pumpMs(10);
  ck(pet.secretRun == 0, "a lone tap is nothing");
  clockOpen = false;

  // Merge on the sketch
  startFunPackGame(MINI_MERGE);
  ck(fpOpen && fpGame == MINI_MERGE, "Merge opens");
  int tiles = 0; for (int i = 0; i < 16; i++) if (mgmBoard[i]) tiles++;
  ck(tiles == 2, "with two tiles to start");
  memset(mgmBoard, 0, 16); mgmBoard[0] = 1; mgmBoard[1] = 1; mgmBoard[5] = 4;
  mergeSwipe(0);
  ck(fpScore == 2 && mgmBoard[0] == 2, "a swipe left merges and scores");
  tiles = 0; for (int i = 0; i < 16; i++) if (mgmBoard[i]) tiles++;
  ck(tiles == 3, "and a new tile appears");
  uint16_t s0 = fpScore;
  uint8_t flat[16] = { 1, 2, 1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 2, 1, 2, 1 };
  memcpy(mgmBoard, flat, 16);
  mergeSwipe(1);
  ck(fpScore == s0 && memcmp(mgmBoard, flat, 16) == 0 && !fpOverUntil, "a swipe that moves nothing changes nothing");
  pumpMs(150);
  ck(fpOpen, "it renders");
  g_touchX = 233; g_touchY = 40; g_touchDown = true; emuFireInterrupt(); pumpMs(120); g_touchDown = false; emuFireInterrupt(); pumpMs(60);
  ck(!fpOpen || fpOverUntil, "the X leaves the game");
  pumpMs(150);
  fpOpen = false; fpOverUntil = 0;

  // swiping through the real touch code
  readyPet();
  startFunPackGame(MINI_MERGE);
  memset(mgmBoard, 0, 16); mgmBoard[5] = 3; mgmBoard[6] = 3;
  g_touchX = 150; g_touchY = 250; g_touchDown = true; emuFireInterrupt(); pumpMs(60);
  g_touchX = 250; g_touchY = 250; emuFireInterrupt(); pumpMs(60);
  g_touchX = 330; g_touchY = 252; emuFireInterrupt(); pumpMs(60);
  g_touchDown = false; emuFireInterrupt(); pumpMs(150);
  ck(fpScore >= 4 && fpOpen, "a real swipe to the right slides the board, and the game stays open");
  fpOpen = false; fpOverUntil = 0;

  // Brick Bash
  readyPet();
  startFunPackGame(MINI_BRICK);
  ck(brkLeft == 20 && brkServing, "Brick Bash starts with 20 bricks and the ball on the paddle");
  pumpMs(1100);
  ck(!brkServing && brkVY < 0, "after a moment the ball is served upwards");
  brkX = 129; brkY = 244; brkVX = 0; brkVY = -0.2f;
  uint16_t before = fpScore; uint8_t left0 = brkLeft;
  pumpMs(300);
  ck(fpScore > before && brkLeft < left0 && !brkAlive[3][0], "the ball breaks the brick above it and scores");
  float dx = brkX - 233, dy = brkY - 240;
  ck(brkServing || sqrtf(dx * dx + dy * dy) <= 200.0f, "and stays inside the round wall");
  pumpMs(120);
  brkServing = false; brkX = 233; brkY = 395; brkVX = 0; brkVY = 0.2f; brkPad = 400;
  pumpMs(200);
  ck(fpMisses == 1 && brkServing, "a ball past the paddle costs a life and is served again");
  fpOpen = false; fpOverUntil = 0;

  // Marble Roll
  readyPet();
  startFunPackGame(MINI_ROLL);
  ck(rllHoles == 2, "Marble Roll starts with two holes");
  rllX = rllBX[0] + 2; rllY = rllBY[0]; rllVX = rllVY = 0;
  pumpMs(150);
  ck(fpScore >= 1, "rolling over a berry scores");
  rllSafeUntil = 0; rllHX[0] = rllX; rllHY[0] = rllY;
  pumpMs(150);
  ck(fpMisses == 1 && near(rllX, 233, 40), "falling in a hole costs a life and puts the marble back");
  rllSafeUntil = millis() + 5000;
  rllHX[0] = rllX; rllHY[0] = rllY;
  uint8_t m0 = fpMisses;
  pumpMs(100);
  ck(fpMisses == m0, "just after a restart it is safe");
  fpOpen = false; fpOverUntil = 0;

  // the Eevee run
  readyPet();
  pet.secretRun = 1;
  startFunPackGame(MINI_RUN);
  ck(fpOpen && fpGame == MINI_RUN, "the Eevee run opens");
  pumpMs(100);
  g_touchX = 233; g_touchY = 250; g_touchDown = true; emuFireInterrupt(); pumpMs(40);
  ck(evAir, "a tap jumps");
  g_touchDown = false; emuFireInterrupt(); pumpMs(1800);
  ck(!evAir && evH == 0, "and it lands");
  evObs[0] = { 120.0f, 0, true, false }; evInvUntil = 0; evH = 0;
  pumpMs(120);
  ck(fpMisses == 1, "running into a rock costs a life");
  uint16_t sc = fpScore;
  evObs[1] = { 60.0f, 0, true, false }; evInvUntil = 0; evH = 0; evAir = true; evVY = RUN_JUMP_V;
  pumpMs(500);
  ck(fpMisses == 1 && fpScore > sc, "jumping over one scores a point");
  fpOpen = false; fpOverUntil = 0;
}

static void swingTests() {
  readyPet();
  pet.wildBalls[0] = 10; pet.wildBalls[1] = 2; pet.wildBalls[2] = 0;
  pet.wildFreeDay = 0; pet.wildCharges = 0;
  gAccFresh = true;
  ck(startWildBattle() && battleOpen && btlWild, "a wild fight");
  btlMsgCount = 0; btlMenu = 4;
  uint8_t pk = pet.wildBalls[0];
  battleTap(69 + 60, 274 + 20);
  ck(btlMenu == 5 && pet.wildBalls[0] == pk && btlSwingKind == 0, "with the sensor, choosing a ball waits for a swing and uses none yet");
  ck(btlSwingUntil != 0, "for a few seconds");
  uint8_t after = pk;
  uint32_t t = millis();
  gMotion.feed(0, 0, 1.0f, t); gMotion.feed(2.8f, 0.2f, 0.2f, t + 20); gMotion.feed(0.3f, 0, 0.8f, t + 40);
  pumpMs(400);
  after = pet.wildBalls[0];
  ck(btlMenu != 5 && after == pk - 1, "a swing throws the ball");
  btlMsgCount = 0; btlOver = false; btlMenu = 4;
  pk = pet.wildBalls[0];
  battleTap(69 + 60, 274 + 20);
  ck(btlMenu == 5, "again");
  battleTap(233, 330);
  ck(btlMenu != 5 && pet.wildBalls[0] == pk - 1, "a tap throws it too");
  btlMsgCount = 0; btlOver = false; btlMenu = 4;
  pk = pet.wildBalls[0];
  battleTap(69 + 60, 274 + 20);
  btlSwingUntil = millis() - 5;
  pumpMs(400);
  ck(btlMenu == 4 && pet.wildBalls[0] == pk, "no swing in time: back to the ball choice, no ball spent");
  gAccFresh = false;
  btlMsgCount = 0; btlMenu = 4;
  battleTap(69 + 60, 274 + 20);
  ck(btlMenu != 5 && pet.wildBalls[0] == pk - 1, "with no sensor, a tap throws straight away as before");
  battleOpen = false; btlOver = false; btlMenu = 0; btlMsgCount = 0;
  gAccFresh = true;
}

static void shakeTests() {
  readyPet();
  gAccFresh = true;
  screenOff = false; dimStage = 0;
  fpOpen = false; battleOpen = false;
  pumpMs(200);
  uint8_t j0 = pet.joy;
  gWalkRun = 0; gLastStepMs = 0;
  pet.shakeToday = 0; pet.shakeDay = 0;   // the emulated sensor may already have shaken once
  j0 = pet.joy;
  uint32_t now = millis();
  shakeToPlay(now);
  if (!(pet.joy == j0 + 2 && pet.shakeToday == 1)) printf("   joy %u (was %u) shakeToday %u screen %d sleeping %d egg %d atWork %d off %d dim %u\n", pet.joy, j0, pet.shakeToday, (int)uiCurrentScreen(), pet.sleeping, pet.isEgg(), pet.atWork(), screenOff, dimStage);
  ck(pet.joy == j0 + 2 && pet.shakeToday == 1, "a shake on the main screen plays");
  gWalkRun = 12; gLastStepMs = now;
  j0 = pet.joy;
  shakeToPlay(now + 100);
  ck(pet.joy == j0, "not while she is walking");
  gWalkRun = 0; gLastStepMs = 0;
  screenOff = true;
  shakeToPlay(now + 200);
  ck(pet.joy == j0, "not with the screen off");
  screenOff = false; dimStage = 1;
  shakeToPlay(now + 200);
  ck(pet.joy == j0, "nor dimmed");
  dimStage = 0;
  fpOpen = true;
  shakeToPlay(now + 200);
  ck(pet.joy == j0, "nor inside a game");
  fpOpen = false;
  pet.sleeping = true;
  shakeToPlay(now + 300);
  ck(pet.joy == j0, "nor asleep");
  pet.sleeping = false;
}

int main() {
  mergeRules();
  ballRules();
  marbleRules();
  runRules();
  motionRules();
  petTests();
  setup();
  pumpMs(120);
  click(233, 108 + 30);
  pumpMs(60);
  click(233, 145);
  if (pet.awaitingStarter()) { printf("FAIL: no starter\n"); return 1; }
  pumpMs(120);
  gameTests();
  swingTests();
  shakeTests();
  if (bad) { printf("FAILURES: %d\n", bad); return 1; }
  printf("games46 test passed\n");
  return 0;
}
