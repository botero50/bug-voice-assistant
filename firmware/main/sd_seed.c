// The first time a microSD card is in the board, Bug puts the example folders on it by itself (skills/ejemplo.txt and home/devices.txt),
// so nobody has to copy files by hand: flashing the firmware (the installer page, scripts\flash.ps1) is enough.
// A marker file on the card, /sd/bugseed.txt, records that it was done: files you edit or delete are never touched again,
// and a different card gets the examples the first time it is used.
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "sd_seed.h"

static const char *TAG = "SD_SEED";

// Embedded from microsd_files/ (see CMakeLists.txt); text files, so they end with a 0 byte
extern const char skills_example_start[] asm("_binary_ejemplo_txt_start");
extern const char skills_example_end[] asm("_binary_ejemplo_txt_end");
extern const char home_example_start[] asm("_binary_devices_txt_start");
extern const char home_example_end[] asm("_binary_devices_txt_end");

static bool write_new(const char *path, const char *start, const char *end, bool overwrite) {
    struct stat info;
    if (!overwrite && stat(path, &info) == 0) return true;  // already there: leave it
    size_t length = (size_t)(end - start);
    while (length > 0 && start[length - 1] == '\0') length--;  // the terminating 0 of an embedded text file
    FILE *file = fopen(path, "wb");
    if (!file) {
        ESP_LOGW(TAG, "Cannot create %s (errno %d)", path, errno);
        return false;
    }
    bool ok = fwrite(start, 1, length, file) == length;
    fclose(file);
    return ok;
}

void sd_seed_defaults(bool force) {
    struct stat info;
    if (!force && stat("/sd/bugseed.txt", &info) == 0) return;  // done on this card before
    mkdir("/sd/skills", 0777);
    mkdir("/sd/home", 0777);
    bool ok = write_new("/sd/skills/ejemplo.txt", skills_example_start, skills_example_end, force);
    ok = write_new("/sd/home/devices.txt", home_example_start, home_example_end, force) && ok;
    if (ok) {
        FILE *marker = fopen("/sd/bugseed.txt", "wb");
        if (marker) {
            fputs("Bug put the example skills and smart-home files on this card. Delete this file to have them restored.\n", marker);
            fclose(marker);
        }
        ESP_LOGI(TAG, "Example skills and smart-home files copied to the card");
    }
}
