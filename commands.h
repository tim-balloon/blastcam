#ifndef COMMANDS_H
#define COMMANDS_H

#include "astrometry.h"
#include "camera.h"

extern int num_clients;
extern int telemetry_sent;
extern int cancelling_auto_focus;
extern int verbose;
extern uint16_t camera_raw[CAMERA_WIDTH * CAMERA_HEIGHT];
void * processClient(void * arg);

#endif