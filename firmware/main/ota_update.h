#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>
#include <stddef.h>

// Updates the firmware over Wi-Fi from the installer page (the same files the web installer uses).
// It looks at the version on the page, and only if that is newer than this firmware does it
// download the new app into the spare slot and restart. A new app that does not start properly
// is replaced by the old one at the next restart (app rollback).

// Starts the check in its own task (does nothing if one is already running).
void ota_update_start(void);

// Tells the bootloader this firmware started well. Call once things work (Wi-Fi up, some seconds in).
void ota_mark_started_ok(void);

// A short message about what the update is doing ("Buscando actualización..."), once per message,
// for the main loop to show. Returns false when there is nothing new.
bool ota_take_message(char *out, size_t size);

#endif
