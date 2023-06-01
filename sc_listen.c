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
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <ueye.h>

#include "sc_listen.h"
#include "commands.h"
#include "camera.h"
#include "lens_adapter.h"


extern int command_lock;
extern int cancelling_auto_focus;
extern int shutting_down;
extern int taking_image;




void set_status(char * ip, char * listen, int value) {
    if (!strcmp(listen, "astrometry"))
    {
        if (!strcmp(ip,TARGET_INET_FC1))
        {
            sock_status.fc1_comm_status = value;
            return;
        } else if (!strcmp(ip,TARGET_INET_FC2))
        {
            sock_status.fc2_comm_status = value;
            return;
        } else if (!strcmp(ip,TARGET_INET_SELF))
        {
            sock_status.self_comm_status = value;
            return;
        } else {
            printf("Invalid IP address to set status\n");
            return;
        }
    } else if (!strcmp(listen, "return")) {
        if (!strcmp(ip,TARGET_INET_FC1))
        {
            sock_status.fc1_return_status = value;
            return;
        } else if (!strcmp(ip,TARGET_INET_FC2))
        {
            sock_status.fc2_return_status = value;
            return;
        } else if (!strcmp(ip,TARGET_INET_SELF))
        {
            sock_status.self_return_status = value;
            return;
        } else {
            printf("Invalid IP address to set status\n");
            return;
        }
    } else {
        if (!strcmp(ip,TARGET_INET_FC1))
        {
            sock_status.fc1_listen_status = value;
            return;
        } else if (!strcmp(ip,TARGET_INET_FC2))
        {
            sock_status.fc2_listen_status = value;
            return;
        } else if (!strcmp(ip,TARGET_INET_SELF))
        {
            sock_status.self_listen_status = value;
            return;
        } else {
            printf("Invalid IP address to set status\n");
            return;
        }
    }
    return;
}

// function to check if aperture value is one of the allowed numbers
int aperture_allowed(float aperture_value) {
    static float allowed_apertures[] = {2.8, 3.0, 3.3, 3.6, 4.0, 4.3, 4.7, 5.1, 5.6, 6.1, 6.7, 7.3, 8.0, 8.7, 9.5, 10.3, 
                          11.3, 12.3, 13.4, 14.6, 16.0, 17.4, 19.0, 20.7, 22.6, 24.6, 26.9, 29.3, 32.0};
    int arrLen = sizeof allowed_apertures / sizeof allowed_apertures[0];
    for (int i = 0; i < arrLen; i++)
    {
        if (allowed_apertures[i] == aperture_value) {
            return 1;
        }
    }
    return 0;
}

int check_AF_params(struct star_cam_capture data) {
    // check to make sure the end-start is an integer multiple of step size
    int distance;
    distance = data.endPos-data.startPos;
    return distance % data.focusStep;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// int command_lock = 0;
// lock that prevents multiple clients from overwriting command data 1 in use 0 free
// int cancelling_auto_focus = 0;
// flag to stop auto focus mid attempt

void process_command_packet(struct star_cam_capture data){
    // dummy variable until I have a structure or 3 to actually modify
    int dummy;
    command_lock = 1; // I am using these now
    // Here we check for in charge
    printf("Processing command data packet from FC%d\n", data.fc);
    printf("in charge received as %d\n", data.inCharge);
    printf("lon is %lf\n", data.longitude);
    if (data.inCharge == 1)
    {
        if (data.update_logOdds == 1)
        {
            printf("Received update to LOGODDS parameter");
            all_astro_params.logodds = data.logOdds;
        }
        if (data.update_lat == 1)
        {
            printf("Received update to LATITUDE parameter\n");
            all_astro_params.latitude = data.latitude;
        }
        if (data.update_lon == 1)
        {
            printf("Received update to LONGITUDE parameter\n");
            all_astro_params.longitude = data.longitude;
        }
        if (data.update_height == 1)
        {
            printf("Received update to HEIGHT parameter\n");
            all_astro_params.hm = data.heightWGS84;
        }
        if (data.update_solveTimeLimit == 1)
        {
            printf("Received update to SOLVE TIME LIMIT parameter\n");
            all_astro_params.timelimit = data.solveTimeLimit;
        }
        if (data.update_focusMode == 1)
        {
            printf("Received update to FOCUS MODE parameter\n");
            // big chunk lifted from XSC code
           if (!data.focusMode && all_camera_params.focus_mode) {
                printf("\n> Cancelling auto-focus process!\n");
                cancelling_auto_focus = 1;
            } else {
                // need to reset cancellation flag to 0 if we are not auto-
                // focusing at all, we are remaining in auto-focusing, or we are
                // entering auto-focusing
                cancelling_auto_focus = 0;
            }
            // performing auto-focusing will restrict some of the other cmds, so
            // check that before other lens commands & hardware adjustments
            if (data.focusMode) {
                all_camera_params.begin_auto_focus = 1;
            }
            all_camera_params.focus_mode = data.focusMode;
        }
        if (data.update_startPos == 1)
        {
            printf("Received update to AUTOFOCUS START POSITION parameter\n");
            printf("Parameter was %d updated to %d\n",all_camera_params.start_focus_pos, data.startPos);
            all_camera_params.start_focus_pos = data.startPos;
        }
        if (data.update_endPos == 1)
        {
            printf("Received update to AUTOFOCUS END POSITION parameter\n");
            all_camera_params.end_focus_pos = data.endPos;
        }
        if (data.update_focusStep == 1)
        {
            printf("Received update to AUTOFOCUS STEP SIZE parameter\n");
            all_camera_params.focus_step = data.focusStep;
        }
        if (data.update_photosPerStep == 1)
        {
            printf("Received update to PHOTOS PER STEP parameter\n");
            all_camera_params.photos_per_focus = data.photosPerStep;
        }
        if (data.update_makeHP == 1)
        {
            printf("Received update to MAKE HOT PIXEL MAP parameter\n");
            all_blob_params.make_static_hp_mask = data.makeHP;
        }
        if (data.update_useHP == 1)
        {
            printf("Received update to USE HOT PIXEL MAP parameter\n");            
            all_blob_params.use_static_hp_mask = data.useHP;
        }
        if (data.update_blobParams[0] == 1)
        {
            printf("Received update to SPIKE LIMIT parameter\n");
            all_blob_params.spike_limit = data.blobParams[0];
        }
        if (data.update_blobParams[1] == 1)
        {
            printf("Received update to DYNAMIC HOT PIXEL parameter\n");
            all_blob_params.dynamic_hot_pixels = data.blobParams[1];
        }
        if (data.update_blobParams[2] == 1)
        {
            printf("Received update to SMOOTHING RADIUS parameter\n");
            all_blob_params.r_smooth = data.blobParams[2];
        }
        if (data.update_blobParams[3] == 1)
        {
            printf("Received update to USE HIGH PASS FILTER parameter\n");
            all_blob_params.high_pass_filter = data.blobParams[3];
        }
        if (data.update_blobParams[4] == 1)
        {
            printf("Received update to HIGH PASS FILTER RADIUS parameter\n");
            all_blob_params.r_high_pass_filter = data.blobParams[4];
        }
        if (data.update_blobParams[5] == 1)
        {
            printf("Received update to CENTROID SEARCH BORDER parameter\n");
            all_blob_params.centroid_search_border = data.blobParams[5];
        }
        if (data.update_blobParams[6] == 1)
        {
            printf("Received update to FILTER RETURN IMAGE parameter\n");
            all_blob_params.filter_return_image = data.blobParams[6];
        }
        if (data.update_blobParams[7] == 1)
        {
            printf("Received update to SIGMA CUTOFF parameter\n");
            all_blob_params.n_sigma = data.blobParams[7];
        }
        if (data.update_blobParams[8] == 1)
        {
            printf("Received update to UNIQUE STAR SPACING parameter\n");
            all_blob_params.unique_star_spacing = data.blobParams[8];
        }
        if (data.update_exposureTime == 1)
        {
            printf("Received update to EXPOSURE TIME parameter\n");
            if (!all_camera_params.focus_mode && !cancelling_auto_focus) {
                // if user adjusted exposure, set exposure to their value
                if (ceil(data.exposureTime) != 
                    ceil(all_camera_params.exposure_time)) {
                    // update value in camera params struct as well
                    all_camera_params.exposure_time = data.exposureTime;
                    all_camera_params.change_exposure_bool = 1;
                }
            } else {
                printf("In or entering auto-focusing mode, or cancelling "
                       "current auto-focus process, so ignore lens " 
                       "commands.\n");
            }
        }
        if (data.update_setFocusInf == 1)
        {
            printf("Received update to SET FOCUS INF parameter\n");
            if (!all_camera_params.focus_mode && !cancelling_auto_focus) {
                // contradictory commands between setting focus to inf and 
                // adjusting it to a different position are handled in 
                // adjustCameraHardware()
                all_camera_params.focus_inf = data.setFocusInf;
            } else {
                printf("In or entering auto-focusing mode, or cancelling "
                       "current auto-focus process, so ignore lens " 
                       "commands.\n");
            }
        }
        if (data.update_focusPos == 1)
        {
            printf("Received update to FOCUS POSITION parameter\n");
            if (!all_camera_params.focus_mode && !cancelling_auto_focus) {
                // if user wants to change focus, change focus in camera params
                all_camera_params.focus_position = data.focusPos;
            } else {
                printf("In or entering auto-focusing mode, or cancelling "
                       "current auto-focus process, so ignore lens " 
                       "commands.\n");
            }
        }
        if (data.update_maxAperture == 1)
        {
            printf("Received update to MAXIMIZE APERTURE parameter\n");
            if (!all_camera_params.focus_mode && !cancelling_auto_focus) {
                // update camera params struct with user commands
                all_camera_params.max_aperture = data.maxAperture;
            } else {
                printf("In or entering auto-focusing mode, or cancelling "
                       "current auto-focus process, so ignore lens " 
                       "commands.\n");
            }
        }
        if (data.update_apertureSteps == 1)
        {
            printf("Received update to APERTURE SIZE parameter\n");
            if (!all_camera_params.focus_mode && !cancelling_auto_focus) {
                // update camera params struct with user commands
                all_camera_params.aperture_steps = data.apertureSteps;
            } else {
                printf("In or entering auto-focusing mode, or cancelling "
                       "current auto-focus process, so ignore lens " 
                       "commands.\n");
            }
        }
        if (!all_camera_params.focus_mode && !cancelling_auto_focus) {
            // if we are taking an image right now, need to wait to execute
            // any lens commands
            while (taking_image) {
                usleep(100000);
            }
            // perform changes to camera settings in lens_adapter.c (focus, 
            // aperture, and exposure deal with camera hardware)
            if (adjustCameraHardware() < 1) {
                printf("Error executing at least one user command.\n");
            }
        } else {
            printf("In or entering auto-focusing mode, or cancelling "
                    "current auto-focus process, so ignore lens " 
                    "commands.\n");
        }
        printf("Packet from FC%d processed.\n",data.fc);
    } else {
        printf("Commands received from not in charge computer, ignoring them...\n");
        return;
    }
    command_lock = 0;
    return;
}

void *listen_thread(void *args){
    struct socket_data * socket_target = args;
    struct star_cam_capture data;
    int listen_target;
    int first_time = 1;
    int sockfd;
    struct addrinfo hints, *servinfo, *servinfoCheck;
    int returnval;
    int numbytes;
    struct sockaddr_storage their_addr;
    char message_str[40];
    socklen_t addr_len;
    char ipAddr[INET_ADDRSTRLEN];
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    int err;
    if (!strcmp(socket_target->ipAddr,TARGET_INET_FC1))
    {
        printf("Listening socket pointed at FC1\n");
        sprintf(message_str,"%s","FC1 listening thread");
        listen_target = 0;
        sock_status.fc1_listen_status = 1;
    } else if (!strcmp(socket_target->ipAddr,TARGET_INET_FC2)) {
        printf("Listening socket pointed at FC2\n");
        sprintf(message_str,"%s","FC2 listening thread");
        listen_target = 1;
        sock_status.fc2_listen_status = 1;
    } else if (!strcmp(socket_target->ipAddr,TARGET_INET_SELF)) {
        printf("Listening socket pointed at self\n");
        sprintf(message_str,"%s","Self listening thread");
        listen_target = 2;
        sock_status.self_listen_status = 1;
    } else {
        printf("Invalid IP target for data source\n");
        return NULL;
    }
    // Fix all this here
    while (!thread_comms.stop_listening[listen_target] && !shutting_down)
    {
        if (first_time)
        {
            first_time = 0;
            if ((returnval = getaddrinfo(NULL, socket_target->port, &hints, &servinfo)) != 0) {
                printf("getaddrinfo: %s\n", gai_strerror(returnval));
                // set status to 0 (dead) if this fails
                set_status(socket_target->ipAddr, socket_target->port, 0);
                return NULL;
            }

            // loop through all the results and bind to the first we can
            for(servinfoCheck = servinfo; servinfoCheck != NULL; servinfoCheck = servinfoCheck->ai_next) {
                if ((sockfd = socket(servinfoCheck->ai_family, servinfoCheck->ai_socktype,
                        servinfoCheck->ai_protocol)) == -1) {
                    perror("listener: socket");
                    continue;
                }

                if (bind(sockfd, servinfoCheck->ai_addr, servinfoCheck->ai_addrlen) == -1) {
                    close(sockfd);
                    // set status to 0 (dead) if this fails
                    set_status(socket_target->ipAddr, socket_target->port, 0);
                    perror("listener: bind");
                    continue;
                }

                break;
            }

            if (servinfoCheck == NULL) {
                // set status to 0 (dead) if this fails
                set_status(socket_target->ipAddr, socket_target->port, 0);
                fprintf(stderr, "listener: failed to bind socket\n");
                return NULL;
            }

            struct timeval read_timeout;
            read_timeout.tv_sec = 0;
            read_timeout.tv_usec = 500000;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);
            // now we set up the print statement vars
            // need to cast the socket address to an INET still address
            struct sockaddr_in *ipv = (struct sockaddr_in *)servinfo->ai_addr;
            // then pass the pointer to translation and put it in a string
            inet_ntop(AF_INET,&(ipv->sin_addr),ipAddr,INET_ADDRSTRLEN);
            printf("Recvfrom target is: %s\n", socket_target->ipAddr);
        }
        numbytes = recvfrom(sockfd, socket_target->camera_commands, sizeof(struct star_cam_capture)+1 , 0,(struct sockaddr *)&their_addr, &addr_len);
        if ( numbytes == -1) {
            err = errno;
            if (err != EAGAIN)
            {
                printf("Errno is %d\n", err);
                printf("Error is %s\n", strerror(err));
                perror("Recvfrom");
            }
        } else {
            process_command_packet(*socket_target->camera_commands);
        }
        if (thread_comms.reset_listen == 1)
        {
            first_time = 1;
            freeaddrinfo(servinfo);
            close(sockfd);
        } else
        {
            usleep(200000);
        }
    }
    freeaddrinfo(servinfo);
    close(sockfd);
    // set status to 0 (dead) if we end
    printf("Socket dying\n");
    set_status(socket_target->ipAddr, socket_target->port, 0);
    return NULL;
}
