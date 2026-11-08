#ifndef SERVER_FINDER_H
#define SERVER_FINDER_H

#include <stdbool.h>

// Finds the PC server on the local network by its name (mDNS service _bugserver._tcp), whatever address the router
// gave it today, and saves that address. Returns true when a server was found. Takes up to two seconds.
bool server_finder_refresh(void);

#endif
