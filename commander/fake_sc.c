/**
 * @file fake_sc.c
 * @author Ian Lowe
 * @brief sends a fake encoder value to the SC looper and receives packets of commands
 * @version 0.1
 * @date 2025-08-20
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include "shared_info.h"

#define INIT_ENCODER_VAL 2883 // doesn't matter as long as it can take the range.

struct timespec ts;
struct socket_data send_info = {.ipAddr = IP_ADDR, .port = ENCODER_PORT};
struct star_cam_return param_data = {.focusPos = INIT_ENCODER_VAL};


// and now we have the vomit parameter packets to MCP version to write!
void *parameter_data_thread(void *args) {
    struct socket_data * socket_target = args;
    int first_time = 1;
    int sockfd;
    struct addrinfo hints, *servinfo, *servinfoCheck;
    int returnval;
    int bytes_sent;
    int length;
    int *retval;
    char message_str[40];
    char ipAddr[INET_ADDRSTRLEN];
    while (1) {
        if (first_time == 1) {
            first_time = 0;
            memset(&hints, 0, sizeof hints);
            hints.ai_family = AF_INET; // set to AF_INET to use IPv4
            hints.ai_socktype = SOCK_DGRAM;
            // fill out address info and return if it fails.
            if ((returnval = getaddrinfo(socket_target->ipAddr, socket_target->port, &hints, &servinfo)) != 0) {
                printf("getaddrinfo: %s\n", gai_strerror(returnval));
                return NULL;
            }
            // now we make a socket with this info
            for (servinfoCheck = servinfo; servinfoCheck != NULL; servinfoCheck = servinfoCheck->ai_next) {
                if ((sockfd = socket(servinfoCheck->ai_family,
                servinfoCheck->ai_socktype, servinfoCheck->ai_protocol)) == -1) {
                    perror("talker: socket");
                    continue;
                }
                break;
            }
            // check to see if we made a socket
            if (servinfoCheck == NULL) {
                // set status to 0 (dead) if this fails
                printf("talker: failed to create socket\n");
                return NULL;
            }
            // if we pass all of these checks then
            // we set up the print statement vars
            // need to cast the socket address to an INET still address
            struct sockaddr_in *ipv = (struct sockaddr_in *)servinfo->ai_addr;
            // then pass the pointer to translation and put it in a string
            inet_ntop(AF_INET, &(ipv->sin_addr), ipAddr, INET_ADDRSTRLEN);
            printf("Parameter thread: IP target is: %s\n", ipAddr);
            // now the "str" is packed with the IP address string
            // first time setup of the socket is done
        }
        if (1) {
            if (!strcmp(socket_target->ipAddr, ipAddr)) {
                length = sizeof(param_data);
                if ((bytes_sent = sendto(sockfd, &param_data, length, 0,
                    servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
                    perror("talker: sendto");
                }
                printf("%s sent packet to %s:%s\n", message_str, ipAddr, socket_target->port);
            } else {
                printf("Param thread: target destination %s differs from thread target %s.\n", socket_target->ipAddr, ipAddr);
            }
        } else {
            printf("Parameter packet could not be packed...\n");
        }
        sleep(1);
    }
    freeaddrinfo(servinfo);
    close(sockfd);
    return NULL;
}




int main(void) {
    pthread_t send_thread;
    pthread_create(&send_thread, NULL, parameter_data_thread, (void *) &send_info);
    pthread_join(send_thread, NULL);
    return 0;
}