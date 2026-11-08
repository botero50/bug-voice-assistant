#ifndef LOCAL_SKILLS_H
#define LOCAL_SKILLS_H

#include <stdbool.h>
#include <stddef.h>

// Skills the board answers by itself, from what it knows (its clock, its battery, its stopwatch...), with its own
// voice (joined voice clips). The PC only turns speech into words; everything here needs no server.

#define LOCAL_MAX_CLIPS 32

typedef struct {
    int clips[LOCAL_MAX_CLIPS];  // what to say, as clip ids
    int clip_count;
    char reply[96];              // the same answer as text, for the screen
    char screen[48];             // action or screen to open, in the same form the PC uses ("volume?d=15", "stopwatch?cmd=start")
    char speak[420];             // text for the online voice, said after the clips
    char speak_lang[8];          // its language ("es" when empty)
    char *json;                  // data for a screen the board built itself (heap, in PSRAM); the caller hands it on or frees it
} local_answer_t;

// If the words are something the board can do itself, fills `answer` and returns true. Otherwise false and the
// words go to the PC.
bool local_skills_try(const char *text, local_answer_t *answer);

// After a local radio/music answer (screen "music_local"): the address to play ("sd:" for the card) and the screen's data
const char *local_radio_url(void);
char *local_radio_screen_json(void);
// The station did not play: the next one's address, or NULL when there are no more
const char *local_radio_failover(void);

// The calendar links (ICS addresses separated by ';') the board reads its agenda from, kept in its memory
bool local_calendar_configured(void);
bool local_mail_set_account(const char *host, const char *user, const char *password);  // empty text removes it
void local_mail_user(char *out, size_t size);  // the mailbox's address, or empty
void local_calendar_set_urls(const char *list);

// Says text with the free online voice (an MP3 from Google Translate's voice service, decoded on the board). Waits until it ends.
// Returns false when there is no internet.
bool tts_say(const char *text);
bool tts_say_lang(const char *text, const char *lang);  // the same in another language ("en", "fr"...)

#endif
