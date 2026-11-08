#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Plays a live radio stream the PC server sends as 16 kHz mono audio. The speaker is shared
// with Bug's voice, so the assistant pauses the music while it listens and answers.

// stream_path is relative to the server, e.g. "music/stream?id=...". Replaces any music playing.
void music_start(const char *stream_path);

// Stops the music and forgets it.
void music_stop(void);

// Stops the stream but remembers it, so music_resume_after_voice() can pick it up again.
void music_pause_for_voice(void);
void music_resume_after_voice(void);

// The stream is running right now.
bool music_is_playing(void);

// Music is on (it may be paused for a moment while Bug talks).
bool music_wanted(void);

// Loudness of the audio now playing, from 0 to 1, for the equalizer.
float music_level(void);

// Sleep timer: the music turns itself off after this many seconds (0 cancels). The main loop asks
// music_sleep_expired() and, when it says true (once), stops the music.
void music_sleep_in(int seconds);
bool music_sleep_expired(void);

// Name of the song now playing from the PC's library (empty for radio). The serial number goes up
// whenever a new song starts, so a screen knows when to redraw.
const char *music_track_title(void);
const char *music_track_artist(void);
int music_track_serial(void);

// A direct station (stream_path starting with http) or the card ("sd:") is played by the board itself.
// Next / previous song of the card (+1 / -1), and whether the card has MP3 files in /sd/music.
void music_next_song(int direction);
bool music_card_has_songs(void);
bool music_card_playing(void);  // music is on and it comes from the card

// Shows another name on the music screen (the board switched station by itself)
void music_set_now_playing(const char *name, const char *detail);

// Plays an MP3 held in memory to the speaker, to the end (used for the online voice).
bool music_play_mp3_memory(const uint8_t *data, size_t size);

void music_set_label(const char *station_name);
const char *music_label(void);

#endif
