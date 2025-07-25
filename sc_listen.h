#ifndef SC_LISTEN_H
#define SC_LISTEN_H

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "sc_data_structures.h"

#define LISTENING_PORT_SELF "4950"
#define LISTENING_PORT_FC1 "4951"
#define LISTENING_PORT_FC2 "4952"
#define TARGET_INET_FC1 "192.168.1.3"
#define TARGET_INET_FC2 "192.168.1.4"
#define TARGET_INET_TEST "192.168.1.225"
#define TARGET_INET_SELF "127.0.0.1"


void set_status(char * ip, char * listen, int value);
int check_AF_params(struct star_cam_capture data);
void process_command_packet(struct star_cam_capture data);
void *listen_thread(void *args);
void * trigger_thread(void* args);
void process_trigger_packet(struct star_cam_trigger data);

#endif