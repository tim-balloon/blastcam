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

#define TARGET_INET_FC1 "192.168.1.3"
#define TARGET_INET_FC2 "192.168.1.4"

// need to choose SC1 or SC2 somehow...
#define SC1_RECEIVE_PARAM_PORT "4950"
#define SC2_RECEIVE_PARAM_PORT "4951"
#define SC1_RECEIVE_SOLVE_PORT "4960"
#define SC2_RECEIVE_SOLVE_PORT "4961"

void *astrometry_data_thread(void *args);
void *parameter_data_thread(void *args);