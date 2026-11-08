#pragma once
// The rules of the games as pure functions, with no screen and no clock, so host tests can drive them directly.
// The Merge rules are from the TamaPoke project (https://github.com/botero50/TamaPoke), MIT licence,
// Copyright (c) 2026 Cheng Ren, Copyright (c) 2026 Quique Tortosa (original TamaPoke). See CREDITS.md.
#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Merge: a 4x4 board. Two tiles of the same number slide together into the next
// number, so the tiles count 1, 2, 3, 4... Directions: 0 left, 1 right, 2 up, 3 down.
// ---------------------------------------------------------------------------
#define MG_N 4
#define MG_MAX_TILE 20

static inline uint8_t mergeCell(uint8_t line, uint8_t pos, uint8_t dir) {
    switch (dir) {
    case 0: return (uint8_t)(line * MG_N + pos);  // left: a row, from its left end
    case 1: return (uint8_t)(line * MG_N + (MG_N - 1 - pos));
    case 2: return (uint8_t)(pos * MG_N + line);  // up: a column, from its top
    default: return (uint8_t)((MG_N - 1 - pos) * MG_N + line);
    }
}

// Slides the whole board. Returns the points it scored (the value of every new tile) and says in *moved
// whether anything changed at all.
static inline uint16_t mergeSlide(uint8_t b[16], uint8_t dir, bool *moved) {
    uint16_t pts = 0;
    bool any = false;
    for (uint8_t line = 0; line < MG_N; line++) {
        uint8_t in[MG_N], n = 0;
        for (uint8_t p = 0; p < MG_N; p++) {
            uint8_t v = b[mergeCell(line, p, dir)];
            if (v) in[n++] = v;
        }
        uint8_t out[MG_N] = {0, 0, 0, 0}, w = 0;
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

// Puts a new tile (mostly a 1, sometimes a 2) on a random empty cell. False if the board is full.
static inline bool mergeSpawn(uint8_t b[16], uint32_t r) {
    uint8_t empty[16], n = 0;
    for (uint8_t i = 0; i < 16; i++) {
        if (!b[i]) empty[n++] = i;
    }
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
