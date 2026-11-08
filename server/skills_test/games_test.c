// Host tests for the rules of the games (firmware/main/games_rules.h). The Merge cases are the ones from TamaPoke's
// tools/emu/tests/games46_test.cpp (MIT), written for C.
#include <stdio.h>
#include <string.h>
#include "../../firmware/main/games_rules.h"
static int fails = 0, checks = 0;
#define ck(cond, msg) do { checks++; if (!(cond)) { fails++; printf("FAIL: %s\n", msg); } } while (0)
int main(void) {
  uint8_t b[16]; bool mv;
  memset(b, 0, 16); b[0] = 1; b[1] = 1; b[2] = 2;
  ck(mergeSlide(b, 0, &mv) == 2 && b[0] == 2 && b[1] == 2 && b[2] == 0 && mv, "[1 1 2] slid left makes [2 2]: a new tile does not merge again");
  memset(b, 0, 16); b[0] = b[1] = b[2] = b[3] = 1;
  ck(mergeSlide(b, 0, &mv) == 4 && b[0] == 2 && b[1] == 2 && b[2] == 0, "four 1s make two 2s, scoring 4");
  memset(b, 0, 16); b[1] = b[2] = 1; mergeSlide(b, 1, &mv);
  ck(b[3] == 2 && b[1] == 0 && b[2] == 0, "sliding right packs to the right edge");
  memset(b, 0, 16); b[0] = 3; b[4] = 3; b[8] = 1;
  ck(mergeSlide(b, 2, &mv) == 4 && b[0] == 4 && b[4] == 1 && b[8] == 0, "up merges a column from the top: 3+3 = 4");
  memset(b, 0, 16); b[3] = 2; b[7] = 2; b[11] = 5; mergeSlide(b, 3, &mv);
  ck(b[15] == 5 && b[11] == 3 && b[7] == 0, "down merges from the bottom");
  uint8_t pack[16] = { 1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6, 4, 5, 6, 7 }, copy[16]; memcpy(copy, pack, 16);
  ck(mergeSlide(pack, 0, &mv) == 0 && !mv && memcmp(pack, copy, 16) == 0, "a slide that changes nothing says so");
  uint8_t stuck[16] = { 1, 2, 1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 2, 1, 2, 1 };
  ck(!mergeCanMove(stuck), "a full board with no equal neighbours has no move");
  stuck[15] = 2; ck(mergeCanMove(stuck), "one equal pair is a move");
  stuck[15] = 0; ck(mergeCanMove(stuck), "an empty cell is a move");
  uint8_t cap[16] = { 0 }; cap[0] = cap[1] = MG_MAX_TILE;
  ck(mergeSlide(cap, 0, &mv) == 0 && cap[0] == MG_MAX_TILE && cap[1] == MG_MAX_TILE, "the top tile does not overflow");
  uint8_t sp[16]; memset(sp, 9, 16); sp[7] = 0;
  ck(mergeSpawn(sp, 12345) && (sp[7] == 1 || sp[7] == 2), "a new tile goes on the empty cell");
  ck(!mergeSpawn(sp, 1), "a full board takes none");
  int ones = 0, twos = 0;
  for (uint32_t r = 0; r < 1000; r++) { uint8_t z[16] = { 0 }; mergeSpawn(z, r); for (int i = 0; i < 16; i++) { if (z[i] == 1) ones++; if (z[i] == 2) twos++; } }
  ck(ones == 900 && twos == 100, "new tiles are mostly 1s, one in ten a 2");
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
