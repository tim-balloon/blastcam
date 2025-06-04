// program to act as the mock star camera computer code
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#include "sc_send.h"
#include "commands.h"
#include "camera.h"
#include "lens_adapter.h"
#include "sc_data_structures.h"

// just a cheapo wrapper function for packing the data into the astrometry packet for the thread
static int populate_astrometry_packet(struct mcp_astrometry * packet_data) {
    packet_data->alt = all_astro_params.alt;
    packet_data->az = all_astro_params.az;
    packet_data->dec_j2000 = all_astro_params.dec_j2000;
    packet_data->dec_observed = all_astro_params.dec;
    packet_data->fr = all_astro_params.fr;
    packet_data->image_rms = all_astro_params.sigma_pointing_as;
    packet_data->ir = all_astro_params.ir;
    packet_data->photo_time = all_astro_params.photo_time;
    packet_data->ps = all_astro_params.ps;
    packet_data->ra_j2000 = all_astro_params.ra_j2000;
    packet_data->ra_observed = all_astro_params.ra;
    packet_data->rawtime = all_astro_params.rawtime;
    return 1;
}

// another cheapo wrapper function for packing the parameter data
static int populate_parameter_packet(struct star_cam_return * packet_data) {
    packet_data->logOdds = all_astro_params.logodds;
    packet_data->latitude = all_astro_params.latitude;
    packet_data->longitude = all_astro_params.longitude;
    packet_data->heightWGS84 = all_astro_params.hm;
    packet_data->exposureTime = all_camera_params.exposure_time;
    packet_data->solveTimeLimit = all_astro_params.timelimit;
    packet_data->focusMode = all_camera_params.focus_position;
    packet_data->minFocusPos = all_camera_params.min_focus_pos;
    packet_data->maxFocusPos = all_camera_params.max_focus_pos;
    packet_data->focusMode = all_camera_params.focus_mode;
    packet_data->startPos = all_camera_params.start_focus_pos;
    packet_data->endPos = all_camera_params.end_focus_pos;
    packet_data->focusStep = all_camera_params.focus_step;
    packet_data->photosPerStep = all_camera_params.photos_per_focus;
    packet_data->setFocusInf = all_camera_params.focus_inf;
    packet_data->apertureSteps = all_camera_params.aperture_steps;
    packet_data->maxAperture = all_camera_params.max_aperture;
    packet_data->aperture = all_camera_params.current_aperture;
    packet_data->makeHP = all_blob_params.make_static_hp_mask;
    packet_data->useHP = all_blob_params.use_static_hp_mask;
    packet_data->blobParams[0] = all_blob_params.spike_limit;
    packet_data->blobParams[1] = all_blob_params.dynamic_hot_pixels;
    packet_data->blobParams[2] = all_blob_params.r_smooth;
    packet_data->blobParams[3] = all_blob_params.high_pass_filter;
    packet_data->blobParams[4] = all_blob_params.r_high_pass_filter;
    packet_data->blobParams[5] = all_blob_params.centroid_search_border;
    packet_data->blobParams[6] = all_blob_params.filter_return_image;
    packet_data->blobParams[7] = all_blob_params.n_sigma;
    packet_data->blobParams[8] = all_blob_params.unique_star_spacing;
    return 1;
}

// rather than using logic and comparing the pointers in socket_data to null, I will just write two send threads
// this has the cost of re-writing a bunch of things but the benefit of each function being simpler
// our omnibus function for vomiting the packets to MCP
void *astrometry_data_thread(void *args) {
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
    int packet_status = 0;
    int which_fc;
    struct mcp_astrometry image_data;
    // set the status of this socket to 1 (started)
    if (!strcmp(socket_target->ipAddr, FC1_IP_ADDR)) {
        which_fc = 0;
        snprintf(message_str, sizeof(message_str), "%s", "FC1 astrometry thread");
        printf("Astrometry socket pointed at FC1\n");
    } else if (!strcmp(socket_target->ipAddr, FC2_IP_ADDR)) {
        which_fc = 1;
        snprintf(message_str, sizeof(message_str), "%s", "FC2 astrometry thread");
        printf("Astrometry socket pointed at FC2\n");
    } else {
        printf("IP provided is %s, expected is %s or %s\n", socket_target->ipAddr, FC1_IP_ADDR, FC2_IP_ADDR);
        printf("Invalid ip target for star camera\n");
        return NULL;
    }
    while (!shutting_down) {
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
            printf("Astrometry thread: IP target is: %s\n", ipAddr);
            // now the "str" is packed with the IP address string
            // first time setup of the socket is done
        }
        // now we pack the packet with the most recent data
        if (image_solved[which_fc])
        {
            packet_status = populate_astrometry_packet(&image_data);
            image_solved[which_fc] = 0;
        }
        if (packet_status) {
            if (!strcmp(socket_target->ipAddr, ipAddr)) {
                packet_status = 0;
                length = sizeof(image_data);
                if ((bytes_sent = sendto(sockfd, &image_data, length, 0,
                    servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
                    perror("talker: sendto");
                }
                printf("%s sent packet to %s:%s\n", message_str, ipAddr, socket_target->port);
            } else {
                printf("Astrometry thread: target destination %s differs from thread target %s.\n", socket_target->ipAddr, ipAddr);
            }
        } else {
            if (image_solved[which_fc] != 0)
            {
                printf("Astrometry packet could not be packed...\n");
            }
        }
        usleep(1000);
    }
    freeaddrinfo(servinfo);
    close(sockfd);
    return NULL;
}

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
    int packet_status = 0;
    struct star_cam_return param_data;
    // set the status of this socket to 1 (started)
    if (!strcmp(socket_target->ipAddr, FC1_IP_ADDR)) {
        snprintf(message_str, sizeof(message_str), "%s", "FC1 parameter thread");
        printf("Parameter socket pointed at FC1\n");
    } else if (!strcmp(socket_target->ipAddr, FC2_IP_ADDR)) {
        snprintf(message_str, sizeof(message_str), "%s", "FC2 parameter thread");
        printf("Parameter socket pointed at FC2\n");
    } else {
        printf("IP provided is %s, expected is %s or %s\n", socket_target->ipAddr, FC1_IP_ADDR, FC2_IP_ADDR);
        printf("Invalid ip target for star camera\n");
        return NULL;
    }
    while (!shutting_down) {
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
        // now we pack the packet with the most recent data
        packet_status = populate_parameter_packet(&param_data);
        if (packet_status) {
            if (!strcmp(socket_target->ipAddr, ipAddr)) {
                packet_status = 0;
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
