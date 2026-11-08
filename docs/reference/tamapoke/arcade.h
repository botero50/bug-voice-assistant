// Phase 46: the rules of the four new games and the motion detector, as pure
// functions with no screen and no clock, so the tests can drive them directly.
#pragma once
#include <stdint.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Merge: a 4x4 board. Two tiles of the same number slide together into the next
// number, so the tiles count 1, 2, 3, 4... Directions: 0 left, 1 right, 2 up, 3 down.
// ---------------------------------------------------------------------------
#define MG_N 4
#define MG_MAX_TILE 20

static inline uint8_t mergeCell(uint8_t line, uint8_t pos, uint8_t dir) {
  switch (dir) {
    case 0:  return (uint8_t)(line * MG_N + pos);                  // left: a row, from its left end
    case 1:  return (uint8_t)(line * MG_N + (MG_N - 1 - pos));
    case 2:  return (uint8_t)(pos * MG_N + line);                  // up: a column, from its top
    default: return (uint8_t)((MG_N - 1 - pos) * MG_N + line);
  }
}

// Slides the whole board. Returns the points it scored (the value of every new
// tile) and says in *moved whether anything changed at all.
static inline uint16_t mergeSlide(uint8_t b[16], uint8_t dir, bool *moved) {
  uint16_t pts = 0;
  bool any = false;
  for (uint8_t line = 0; line < MG_N; line++) {
    uint8_t in[MG_N], n = 0;
    for (uint8_t p = 0; p < MG_N; p++) { uint8_t v = b[mergeCell(line, p, dir)]; if (v) in[n++] = v; }
    uint8_t out[MG_N] = { 0, 0, 0, 0 }, w = 0;
    for (uint8_t i = 0; i < n; i++) {
      if (i + 1 < n && in[i] == in[i + 1] && in[i] < MG_MAX_TILE) {
        out[w++] = (uint8_t)(in[i] + 1);
        pts += (uint16_t)(in[i] + 1);
        i++;
      } else {
        out[w++] = in[i];
      }
    }
    for (uint8_t p = 0; p < MG_N; p++) {
      uint8_t c = mergeCell(line, p, dir);
      if (b[c] != out[p]) any = true;
      b[c] = out[p];
    }
  }
  if (moved) *moved = any;
  return pts;
}

// Puts a new tile (mostly a 1, sometimes a 2) on a random empty cell. False if full.
static inline bool mergeSpawn(uint8_t b[16], uint32_t r) {
  uint8_t empty[16], n = 0;
  for (uint8_t i = 0; i < 16; i++) if (!b[i]) empty[n++] = i;
  if (!n) return false;
  b[empty[(r >> 4) % n]] = (r % 10 == 0) ? 2 : 1;
  return true;
}

static inline bool mergeCanMove(const uint8_t b[16]) {
  for (uint8_t i = 0; i < 16; i++) {
    if (!b[i]) return true;
    if ((i % MG_N) + 1 < MG_N && b[i] == b[i + 1]) return true;
    if (i + MG_N < 16 && b[i] == b[i + MG_N]) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Bouncing balls: Brick Bash. All of it works on a ball (x, y) moving at (vx, vy).
// ---------------------------------------------------------------------------

// The round wall: a ball that would leave the circle is put back inside and bounced.
static inline bool wallBounce(float &x, float &y, float &vx, float &vy, float cx, float cy, float R, float r) {
  float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
  if (d <= R - r || d < 0.001f) return false;
  float nx = dx / d, ny = dy / d, dot = vx * nx + vy * ny;
  if (dot > 0) { vx -= 2 * dot * nx; vy -= 2 * dot * ny; }
  x = cx + nx * (R - r);
  y = cy + ny * (R - r);
  return true;
}

// The paddle: only a ball coming down is caught. Where it lands steers it: the
// middle sends it straight up, the ends send it off at a slant. Speed is kept.
static inline bool paddleBounce(float &x, float &y, float &vx, float &vy, float padX, float padY, float half, float r) {
  if (vy <= 0) return false;
  if (y + r < padY || y - r > padY + 14) return false;
  if (fabsf(x - padX) > half + r) return false;
  float spd = sqrtf(vx * vx + vy * vy);
  float off = (x - padX) / (half + r);
  if (off > 1) off = 1;
  if (off < -1) off = -1;
  vx = spd * off * 0.85f;
  float vy2 = spd * spd - vx * vx;
  vy = -sqrtf(vy2 > 0 ? vy2 : 0);
  y = padY - r;
  return true;
}

// A brick (a rectangle): bounce off the side the ball went into the least.
static inline bool rectBounce(float &x, float &y, float &vx, float &vy, float r, float rx, float ry, float rw, float rh) {
  if (x < rx - r || x > rx + rw + r || y < ry - r || y > ry + rh + r) return false;
  float left = x - (rx - r), right = (rx + rw + r) - x, top = y - (ry - r), bottom = (ry + rh + r) - y;
  float mx = left < right ? left : right, my = top < bottom ? top : bottom;
  if (mx < my) vx = (left < right) ? -fabsf(vx) : fabsf(vx);
  else vy = (top < bottom) ? -fabsf(vy) : fabsf(vy);
  return true;
}

// A ball that has drifted almost flat would take forever: keep it climbing a bit.
static inline void keepSlanted(float &vx, float &vy) {
  float spd = sqrtf(vx * vx + vy * vy);
  if (spd < 0.001f) return;
  float minVy = spd * 0.28f;
  if (fabsf(vy) < minVy) {
    vy = vy < 0 ? -minVy : minVy;
    float vx2 = spd * spd - vy * vy;
    vx = (vx < 0 ? -1.0f : 1.0f) * sqrtf(vx2 > 0 ? vx2 : 0);
  }
}

// ---------------------------------------------------------------------------
// Marble Roll: a marble in a round tray, pulled by tilt. Friction keeps it calm.
// ---------------------------------------------------------------------------
static inline void marbleStep(float &x, float &y, float &vx, float &vy, float ax, float ay, float dt,
                              float cx, float cy, float R, float r) {
  vx = (vx + ax * dt) * 0.97f;
  vy = (vy + ay * dt) * 0.97f;
  const float cap = 0.30f;                       // px per ms: quick, but never through a berry
  if (vx > cap) vx = cap;
  if (vx < -cap) vx = -cap;
  if (vy > cap) vy = cap;
  if (vy < -cap) vy = -cap;
  x += vx * dt;
  y += vy * dt;
  float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
  if (d > R - r && d > 0.001f) {                 // the rim: it stops there rather than bouncing off
    x = cx + dx / d * (R - r);
    y = cy + dy / d * (R - r);
    float dot = vx * dx / d + vy * dy / d;
    if (dot > 0) { vx -= dot * dx / d; vy -= dot * dy / d; }
  }
}

static inline bool within(float ax, float ay, float bx, float by, float d) {
  float dx = ax - bx, dy = ay - by;
  return dx * dx + dy * dy <= d * d;
}

// ---------------------------------------------------------------------------
// Eevee Run: one tap, one jump. y is the height above the ground in pixels.
// ---------------------------------------------------------------------------
#define RUN_JUMP_V 0.62f     // px per ms upward at take-off
#define RUN_GRAVITY 0.0017f  // px per ms squared

// Advances a jump by dt ms. Returns true while it is still in the air.
static inline bool runJumpStep(float &h, float &vy, float dt) {
  h += vy * dt;
  vy -= RUN_GRAVITY * dt;
  if (h <= 0) { h = 0; vy = 0; return false; }
  return true;
}

// The longest a jump can be held up: about 2 * RUN_JUMP_V / RUN_GRAVITY ms.
static inline float runJumpMs() { return 2 * RUN_JUMP_V / RUN_GRAVITY; }
static inline float runJumpPeak() { return RUN_JUMP_V * RUN_JUMP_V / (2 * RUN_GRAVITY); }

// An obstacle of width w and height oh at screen x ox; Eevee is a box of width ew
// standing at ex, h pixels up. True when they touch.
static inline bool runHit(float ex, float ew, float h, float ox, float ow, float oh) {
  return ex + ew > ox && ex < ox + ow && h < oh;
}

// ---------------------------------------------------------------------------
// Motion: a swing (one hard jolt, to throw a ball) and a shake (several back and
// forth, to play). Both read the same stream of accelerometer samples that the
// step counter reads, without touching it. Units are g.
// ---------------------------------------------------------------------------
struct MotionDetector {
  float swingG = 1.1f;             // how far past 1 g a single jolt must go
  float shakeG = 0.75f;            // how far past 1 g each back-and-forth must go
  uint8_t shakeNeed = 4;           // this many in shakeMs
  uint16_t shakeMs = 1300;
  uint16_t sameJoltMs = 150;       // samples this close belong to the same jolt

  bool swingEvent = false, shakeEvent = false;
  uint32_t peaks[8] = { 0 };
  uint8_t npeaks = 0;
  uint32_t lastShake = 0;

  void feed(float x, float y, float z, uint32_t ms) {
    float d = fabsf(sqrtf(x * x + y * y + z * z) - 1.0f);
    if (d < shakeG) return;
    if (npeaks && ms - peaks[npeaks - 1] < sameJoltMs) {          // the tail of the same jolt
      if (d >= swingG) swingEvent = true;
      return;
    }
    if (npeaks == 8) { for (uint8_t i = 1; i < 8; i++) peaks[i - 1] = peaks[i]; npeaks = 7; }
    peaks[npeaks++] = ms;
    if (d >= swingG) swingEvent = true;
    uint8_t recent = 0;
    for (uint8_t i = 0; i < npeaks; i++) if (ms - peaks[i] <= shakeMs) recent++;
    if (recent >= shakeNeed && (!lastShake || ms - lastShake > 1500)) { shakeEvent = true; lastShake = ms; npeaks = 0; }
  }
  bool takeSwing() { bool s = swingEvent; swingEvent = false; return s; }
  bool takeShake() { bool s = shakeEvent; shakeEvent = false; return s; }
  void clear() { swingEvent = shakeEvent = false; npeaks = 0; }
};
