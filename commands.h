#include "astrometry.h"
#ifndef COMMANDS_H
#define COMMANDS_H

extern int num_clients;
extern int telemetry_sent;
extern int cancelling_auto_focus;
extern int verbose;
extern uint16_t* camera_raw;
void * processClient(void * arg);
extern struct mcp_astrometry mcp_astro;
#endif