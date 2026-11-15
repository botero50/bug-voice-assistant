#ifndef CONFIG_BACKUP_H
#define CONFIG_BACKUP_H

#include <stdbool.h>

// Keeps a copy of everything the board remembers (Wi-Fi, server, e-mail, agenda, tokens, smart home, registered voices, ...)
// on the microSD card, in /sd/bugcfg.js. Reinstalling the firmware with "Erase device" wipes the board's own memory
// but not the card, so the board restores itself from the card the next time it starts. The file holds passwords in plain
// text: keep the card with the board, and use BUGCFG {"backup":0} (or {"forget":"all"}) to delete it.

// If the board's memory has no Wi-Fi saved and the card has a copy, restores it and returns true (the caller restarts the board)
bool config_backup_restore_if_needed(void);

// Writes the copy now if anything changed since the last one (does nothing when the card is missing or the backup is off)
void config_backup_save(void);

// Called often from the assistant's loop: saves a minute after a change was noticed (cheap when nothing changed)
void config_backup_poll(void);

// Asks for a save soon (after a BUGCFG order, for example)
void config_backup_request(void);

void config_backup_enable(bool on);  // off also deletes the copy on the card
bool config_backup_enabled(void);
void config_backup_delete(void);

#endif
