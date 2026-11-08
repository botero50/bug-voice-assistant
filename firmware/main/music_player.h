#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

#include <stdbool.h>

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

// Name of the song now playing from the PC's library (empty for radio). The serial number goes up
// whenever a new song starts, so a screen knows when to redraw.
const char *music_track_title(void);
const char *music_track_artist(void);
int music_track_serial(void);

void music_set_label(const char *station_name);
const char *music_label(void);

#endif
