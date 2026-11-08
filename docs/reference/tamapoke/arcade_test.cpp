// Phase 26: the arcade -- tickets, the prize counter, this week's bests and the
// record badges.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
uint32_t g_seed=26; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
static uint32_t g_ms=0;
uint32_t millis(){return g_ms;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static const uint32_t DAY = 86400;
static uint32_t atDay(uint32_t d, int h){ return d * DAY + (uint32_t)h * 3600 + 30; }

static void fresh(Pet &p, uint32_t day){
  p.begin();
  p.dbgHatchAs(1,false);
  p.sleeping = false;
  p.sickness = SICK_NONE;
  p.fullness = p.joy = p.energy = p.hygiene = 80;
  p.coins = 0; p.coinsToday = 0; p.coinDay = day;
  p.lastSeenEpoch = atDay(day, 12);
  p.tickets = 0; p.ticketDay = 0; p.ticketsToday = 0;
  for (int g = 0; g < MINI_COUNT; g++) { p.miniHi[g] = 0; p.weekHi[g] = 0; }
  p.weekId = 0;
  p.hatsOwned = 0; p.hatOn = -1; p.decorOwned = 0;
}

int main(){
  // --- tickets
  {
    Pet p; fresh(p, 20400);
    p.applyMiniResult(MINI_LR, 40);
    ck(p.lastTickets == 3 + TICKET_RECORD_BONUS && p.tickets == 6, "a first score of 40: 1 + 40/20, plus 3 for the record");
    p.applyMiniResult(MINI_LR, 40);
    ck(p.lastTickets == 3 && p.tickets == 9, "the same score again: no record bonus");
    p.applyMiniResult(MINI_HL, 200);
    ck(p.lastTickets == TICKET_GAME_MAX + TICKET_RECORD_BONUS, "a huge score pays at most 5, plus the record");
    for (int i = 0; i < 20; i++) { p.energy = 100; p.applyMiniResult(MINI_LR, 10); }
    ck(p.ticketsToday == TICKETS_PER_DAY && p.tickets == TICKETS_PER_DAY, "never more than 30 tickets a day");
    p.energy = 100; p.applyMiniResult(MINI_LR, 10);
    ck(p.lastTickets == 0, "the 31st pays nothing");
    p.lastSeenEpoch = atDay(20401, 12);
    p.energy = 100; p.applyMiniResult(MINI_LR, 10);
    ck(p.lastTickets == 1 && p.ticketsToday == 1, "a new day: tickets again");
  }

  // --- the prize counter
  {
    Pet p; fresh(p, 20400);
    p.tickets = Pet::prizePrice(PRIZE_CAP) - 1;
    ck(p.winPrize(PRIZE_CAP) == PRZ_NO_TICKETS && !p.ownsHat(HAT_ARCADE), "one ticket short: no cap");
    p.tickets = Pet::prizePrice(PRIZE_CAP);
    ck(p.winPrize(PRIZE_CAP) == PRZ_OK && p.ownsHat(HAT_ARCADE) && p.tickets == 0, "60 tickets: the arcade cap");
    p.tickets = 500;
    ck(p.winPrize(PRIZE_CAP) == PRZ_OWNED && p.tickets == 500, "once owned it cannot be won again");
    ck(p.winPrize(PRIZE_VISOR) == PRZ_OK && p.ownsHat(HAT_VISOR), "the star visor");
    ck(p.winPrize(PRIZE_CABINET) == PRZ_OK && p.ownsDecor(DEC_CABINET), "the arcade cabinet");
    uint8_t cake = p.foodCount[FOOD_CAKE];
    ck(p.winPrize(PRIZE_CAKE) == PRZ_OK && p.foodCount[FOOD_CAKE] == cake + 1, "a cake for the bag");
    uint32_t coins = p.coins;
    ck(p.winPrize(PRIZE_CAPSULE) == PRZ_OK && p.coins >= coins, "a capsule spin costs tickets, not coins");
    // never sold, never from a capsule or a quest
    Pet q; fresh(q, 20400);
    q.coins = 100000;
    ck(!q.buyHat(HAT_ARCADE) && !q.buyHat(HAT_VISOR) && !q.buyDecor(DEC_CABINET), "the prizes are not for sale");
    for (int i = 0; i < 600; i++) q.spinCapsule(false);
    ck(!q.ownsHat(HAT_ARCADE) && !q.ownsHat(HAT_VISOR) && !q.ownsDecor(DEC_CABINET), "600 capsules never give a prize");
    ck(q.ownsHat(HAT_CAP) || q.hatsOwned, "while they do give the shop's hats");
  }

  // --- badges
  {
    ck(Pet::badgeTier(MINI_LR, 4) == 0 && Pet::badgeTier(MINI_LR, 5) == 1 &&
       Pet::badgeTier(MINI_LR, 10) == 2 && Pet::badgeTier(MINI_LR, 15) == 3, "bronze, silver, gold at 1x, 2x, 3x the good score");
    ck(Pet::badgeTier(MINI_BUBBLES, 60) == 2, "each game on its own scale (bubbles: 30 is good)");
    Pet p; fresh(p, 20400);
    p.applyMiniResult(MINI_LR, 15);
    ck(p.lastBadge == 3 && p.goldBadges() == 1, "a first 15 at Left or Right: straight to gold");
    p.energy = 100; p.applyMiniResult(MINI_LR, 15);
    ck(p.lastBadge == 0, "and only once");
  }

  // --- this week's bests
  {
    Pet p; fresh(p, 20400);
    p.applyMiniResult(MINI_MOLE, 12);
    ck(p.weekHi[MINI_MOLE] == 12 && p.beatThisWeek(MINI_MOLE), "this week's best, a record too");
    p.energy = 100; p.applyMiniResult(MINI_MOLE, 8);
    ck(p.weekHi[MINI_MOLE] == 12, "a lower score keeps the week's best");
    uint32_t w = p.weekId;
    p.lastSeenEpoch = atDay(20400 + 7, 12);
    p.weekRoll();
    ck(p.weekId == w + 1 && p.weekHi[MINI_MOLE] == 0 && p.miniHi[MINI_MOLE] == 12, "a new week: the board starts again, the record stays");
    // weeks start on Monday: day 20395 (1970-01-01 was a Thursday) is a Monday
    p.lastSeenEpoch = atDay(20394, 12); p.weekRoll(); uint32_t sun = p.weekId;
    p.lastSeenEpoch = atDay(20395, 12); p.weekRoll();
    ck(p.weekId == sun + 1, "Sunday to Monday is a new week");
    // it all persists
    p.tickets = 77; p.weekHi[MINI_MOLE] = 9; p.setClock(p.lastSeenEpoch);   // setClock saves
    Pet q; q.begin();
    ck(q.tickets == 77 && q.weekHi[MINI_MOLE] == 9 && q.weekId == p.weekId, "tickets and the week's bests persist");
  }

  // --- phase 27: the two arcade trophies, past the old limit of 16
  {
    Pet p; fresh(p, 20400);
    p.trophies = 0; p.coinsEarned = 0;
    for (int g = 0; g < 9; g++) p.miniHi[g] = 250;   // nine golds
    ck(p.goldBadges() == 9, "nine gold badges");
    while (p.checkTrophies() >= 0) {}                 // whatever else is already earned
    ck(!p.hasTrophy(TRO_GOLD), "nine golds: not yet");
    p.miniHi[9] = 250;
    uint32_t c0 = p.coins;
    ck(p.checkTrophies() == TRO_GOLD && p.hasTrophy(TRO_GOLD) && p.coins == c0 + TROPHY_COINS, "the tenth gold: Gold collector, and its coins");
    ck(p.checkTrophies() != TRO_GOLD, "and only once");
    p.tickets = 500;
    p.winPrize(PRIZE_CAP); p.winPrize(PRIZE_VISOR);
    while (p.checkTrophies() >= 0) {}
    ck(!p.hasTrophy(TRO_PRIZES), "two prizes: not yet");
    p.winPrize(PRIZE_CABINET);
    ck(p.checkTrophies() == TRO_PRIZES && p.hasTrophy(TRO_PRIZES), "all three: Prize winner");
    ck(TRO_PRIZES >= 16 && (p.trophies >> 16) != 0, "these live past bit 16");
    p.setClock(p.lastSeenEpoch);
    Pet q; q.begin();
    ck(q.hasTrophy(TRO_GOLD) && q.hasTrophy(TRO_PRIZES) && q.trophies == p.trophies, "and they persist");
  }

  if (bad) printf("FAILURES\n");
  return bad?1:0;
}
