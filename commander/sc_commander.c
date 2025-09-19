/**
 * @file sc_commander.c
 * @author Ian Lowe
 * @brief receives encoder values from the star camera and uses them to determine
 * the appropriate commands to send back to the star camera for use on FTS test flight
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

#define FNAME_MAX_LEN 60
#define SAVE_FILE "/home/starcam/Desktop/TIMSC/commands.txt"
#define DEFAULT_ENC_TESTING 2850
FILE * fptr = NULL;

struct star_cam_return ret_packet = {0};
struct star_cam_capture command_packet = {0};
struct socket_data recv_info = {.ipAddr = IP_ADDR, .port = ENCODER_PORT};
struct socket_data send_info = {.ipAddr = IP_ADDR, .port = COMMAND_PORT};
int have_received_enc_value = 0; // flag to start doing actual stuff
float focus_pos[30] = {0}; // don't do anything until we receive the first 30 focus position packets so we can assume thats a good estimate of starting focus pos.
double gainvals[4] = {1,2,3,4};
double exposureValsMs[3] = {(0.05 * 1000.0), (0.1 * 1000.0), (0.2 * 1000.0)};
float enc_position_deltas[7] = {-30,-20,-10,0,10,20,30}; // move to -30 initially then step +10
int focus_mode[2] = {0,1};
float focus_mode_pos_testing = 2973;

double time_tag(){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double temp = (double)ts.tv_sec + ((double)ts.tv_nsec*1e-9);
    return temp;
}

void process_enc_packet(struct star_cam_return packet) {
    static int counter = 0;
    if (counter < 30)
    {
        focus_pos[counter] = packet.focusPos;
        counter++;
    }
    else if (counter == 30)
    {
        counter++;
        printf("received encoder values: ");
        for (int i = 0; i < 30; i++)
        {
            printf("%f ", focus_pos[i]);
        }
        printf("\n");
        have_received_enc_value = 1;
        return;
    }
}

/**
 * @brief function called in a p_thread that listens for the star camera parameter updates
 * 
 * @param args struct socketData * filled with IP address and port, but cast to a void* for p_thread
 * @return void* set to null when we return since we don't use the values.
 */
void *parameter_receive_thread(void *args) {
    struct socket_data * socketinfo = args;
    int sleep_interval_usec = 200000;
    int first_time = 1;
    int sockfd;
    int sleep_length;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *servinfoCheck;
    int returnval;
    int numbytes;
    int listening = 1;
    int which_sc;
    struct sockaddr_storage sender_addr;
    struct star_cam_return received_parameters;
    socklen_t addr_len = sizeof(struct sockaddr_storage);
    char ipAddr[INET_ADDRSTRLEN];
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    int err;
    // Fix all this here
    while (1) {
        if (first_time) {
            first_time = 0;
            if ((returnval = getaddrinfo(NULL, socketinfo->port, &hints, &servinfo)) != 0) {
                printf("getaddrinfo: %s\n", gai_strerror(returnval));
                return NULL;
            }
            // loop through all the results and bind to the first we can
            for (servinfoCheck = servinfo; servinfoCheck != NULL; servinfoCheck = servinfoCheck->ai_next) {
                // since a success needs both of these, but it is inefficient to do fails twice,
                // continue skips the second if on a fail
                if ((sockfd = socket(servinfoCheck->ai_family, servinfoCheck->ai_socktype,
                        servinfoCheck->ai_protocol)) == -1) {
                    perror("listener: socket");
                    continue;
                }
                if (bind(sockfd, servinfoCheck->ai_addr, servinfoCheck->ai_addrlen) == -1) {
                    close(sockfd);
                    perror("listener: bind");
                    continue;
                }
                break;
            }
            if (servinfoCheck == NULL) {
                printf("listener: failed to bind socket\n");
                return NULL;
            }
            // set the read timeout (if there isnt a message) to 100ms
            struct timeval read_timeout;
            int read_timeout_usec = 100000;
            read_timeout.tv_sec = 0;
            read_timeout.tv_usec = read_timeout_usec;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
            // now we set up the print statement vars
            // need to cast the socket address to an INET still address
            struct sockaddr_in *ipv = (struct sockaddr_in *)servinfo->ai_addr;
            // then pass the pointer to translation and put it in a string
            inet_ntop(AF_INET, &(ipv->sin_addr), ipAddr, INET_ADDRSTRLEN);
            printf("Parameter receiving target is: %s\n", socketinfo->ipAddr);
        }
        numbytes = recvfrom(sockfd, &ret_packet, sizeof(ret_packet),
         0, (struct sockaddr *)&sender_addr, &addr_len);
        // we get an error everytime it times out, but EAGAIN is ok, other ones are bad.
        if (numbytes == -1) {
            err = errno;
            if (err != EAGAIN) {
                perror("Recvfrom");
            }
        } else {
            process_enc_packet(ret_packet);
        }
        usleep(10000);
    }
    freeaddrinfo(servinfo);
    close(sockfd);
    return NULL;
}

void clear_packet(struct star_cam_capture * packet) {
    packet->update_focusMode = 0;
    packet->focusMode = 0;
    packet->update_focusPos = 0;
    packet->focusPos = 0;
    packet->update_gainFact = 0;
    packet->gainFact = 0;
    packet->update_exposureTime = 0;
    packet->exposureTime = 0;
    packet->update_startPos = 0;
    packet->update_endPos = 0;
    packet->update_focusStep = 0;
}


int generate_pre_AF_packet(struct star_cam_capture * packet, FILE * fptr, int* flag_to_af) {
    clear_packet(packet);
    packet->fc = 1;
    packet->inCharge = 1;
    packet->update_gainFact = 1;
    packet->gainFact = 1.0;
    packet->update_exposureTime = 1;
    // Synchronized with a 4x binning factor during AF, in order to maintain
    // nominal pixel values equivalent to a 100ms exposure
    packet->exposureTime = 25.0;
    *flag_to_af = 2;
    int ret = fprintf(fptr, "PRE AUTOFOCUS CHANGES, %.4lf, %.4lf, %.4lf\n", packet->gainFact, packet->exposureTime, time_tag());
    if(ret < 1) {
    	perror("|||||||||||||||||||||||||||||||||||||||||| file print failed: ");
    }
    // fflush(fptr);
    return 1;
}


int generate_AF_packet(struct star_cam_capture * packet, FILE * fptr, int* flag_to_af) {
    clear_packet(packet);
    packet->fc = 1;
    packet->inCharge = 1;
    packet->update_focusMode = 1;
    packet->focusMode = 1;

    int defaultPos = (int)focus_pos[29];
    packet->startPos = defaultPos - 500;
    packet->update_startPos = 1;
    packet->endPos = defaultPos + 400;
    packet->update_endPos = 1;
    packet->focusStep = 5;
    packet->update_focusStep = 1;
    *flag_to_af = 0;
    int ret = fprintf(fptr, "############ AUTOFOCUSING ########### %.4lf\n", time_tag());
    if(ret < 1) {
        perror("|||||||||||||||||||||||||||||||||||||||||| file print failed: ");
    }
    // fflush(fptr);
    return 130; // sleep after sending the AF packet to make sure it completes its task
}

int generate_command_packet(struct star_cam_capture * packet, FILE * fptr, int* flag_to_af) { // eventually generate, right now lets dump it in a file
    static int first_time = 1;
    static int have_sent_AF = 0;
    static int i = 0, j = 0, k = 0; // fake ass loop variables
    if (first_time == 1)
    {
	printf("I am trying to write my header\n");
        int ret = fprintf(fptr, "#ENC POSITION, GAIN, EXPOSURE, CLOCKTIME\n");
	printf("%d\n", ret);
        if(ret < 1) {
        	perror("|||||||||||||||||||||||||||||||||||||||||| file print failed: ");
        }
        // fflush(fptr);
        first_time = 0;
    }
    // generate the packet here
    clear_packet(packet);
    packet->fc = 1;
    packet->inCharge = 1;
    packet->update_focusMode = 1;
    packet->focusMode = 0; // make sure we are always in the manual focus when sending these packets
    // this is the normal packet that accesses the ∂ structure
    if (i < 7)
    {
        packet->update_focusPos = 1;
        packet->focusPos = focus_pos[29] + enc_position_deltas[i];
        packet->update_gainFact = 1;
        packet->gainFact = gainvals[j];
        packet->update_exposureTime = 1;
        packet->exposureTime = exposureValsMs[k];
        int ret = fprintf(fptr, "%.4f, %.4lf, %.4lf, %.4lf\n", packet->focusPos, packet->gainFact, packet->exposureTime, time_tag());
        if(ret < 1) {
        	perror("|||||||||||||||||||||||||||||||||||||||||| file print failed: ");
        }
	// fflush(fptr);
    }
    // this is the post AF packet that doesn't change the focus pos
    else
    {
        packet->update_gainFact = 1;
        packet->gainFact = gainvals[j];
        packet->update_exposureTime = 1;
        packet->exposureTime = exposureValsMs[k];
        int ret = fprintf(fptr, "AF LOCATION, %.4lf, %.4lf, %.4lf\n", packet->gainFact, packet->exposureTime, time_tag());
        if(ret < 1) {
        	perror("|||||||||||||||||||||||||||||||||||||||||| file print failed: ");
    	}
	// fflush(fptr);
    }
    // stupid way for me to iterate through the parameter space
    k++;
    if (k == 3)
    {
        k = 0;
        j++;
    }
    if (j == 4)
    {
        j = 0;
        i++;
    }
    if (i == 8)
    {
        i = 0;
        int ret = fprintf(fptr, "######### END OF CYCLE ##########\n");
        if(ret < 1) {
        	perror("|||||||||||||||||||||||||||||||||||||||||| file print failed: ");
    	}
	// fflush(fptr);
    }
    if (i == 7 && j == 0 && k == 0) // has just moved to the AF setting.
    {
        *flag_to_af = 1; // we will autofocus on the next loop
    }
    return 150;
}

// steal a send command packet thread from MCP
/**
 * @brief our omnibus function for vomiting the packets to the star camera
 * 
 * @param args p_threads take a void pointer that must be typecast to the
 * appropriate argument after resolving the threaded function call. In this case it takes type
 * struct socketData * which must be populated with the IP address and port of the star camera
 * to talk to
 * @return void* We just return null at the end, another p_thread requirement that this is a pointer
 */
void *star_camera_command_thread(void *args) {
    struct socket_data * socket_target = args;
    int first_time = 1;
    int sleep_interval_sec = 0; // set by the return functions
    int AF_flag = 0;
    int sockfd;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *servinfoCheck;
    int returnval;
    int bytes_sent;
    int length;
    int *retval;
    char message_str[40];
    char ipAddr[INET_ADDRSTRLEN];
    int sending = 1;
    int which_sc;
    int packet_status = 0;
    while (1) {
        if (first_time == 1) {
            first_time = 0;
            memset(&hints, 0, sizeof(hints));
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
        printf("IP target is: %s\n", ipAddr);
        // now the "str" is packed with the IP address string
        // first time setup of the socket is done
        }
        // Here we move on the main portion of the data comms
        // check to see if a command packet has been packed
        if (have_received_enc_value)
        {
	    fptr = fopen(SAVE_FILE, "a");
            if (AF_flag == 0)
            {
                sleep_interval_sec = generate_command_packet(&command_packet, fptr, &AF_flag);
            } else if (AF_flag == 1) {
                sleep_interval_sec = generate_pre_AF_packet(&command_packet, fptr, &AF_flag);
            } else if (AF_flag == 2) {
                sleep_interval_sec = generate_AF_packet(&command_packet, fptr, &AF_flag);
            }
	    fclose(fptr);
            if (1) { // no need to check now, packing is done here
                if (!strcmp(socket_target->ipAddr, ipAddr)) {
                    packet_status = 0;
                    length = sizeof(command_packet);
                    if ((bytes_sent = sendto(sockfd, &command_packet, length, 0,
                        servinfo->ai_addr, servinfo->ai_addrlen)) == -1) {
                        perror("talker: sendto");
                    }
                } else {
                    printf("Target destination differs from thread target.\n");
                }
            }
            sleep(sleep_interval_sec);
        } else {
            sleep(1); // sleep for a second in between 
        }
    }
    freeaddrinfo(servinfo);
    close(sockfd);
    return NULL;
}

int main(void) {
    char filename[FNAME_MAX_LEN];
    snprintf(filename, FNAME_MAX_LEN, "%s", SAVE_FILE);
    fptr = fopen(filename, "w");
    fclose(fptr);
    printf("%s\n",filename);
    pthread_t recv_thread, send_thread;
    pthread_create(&send_thread, NULL, star_camera_command_thread, (void *) &send_info);
    pthread_create(&recv_thread, NULL, parameter_receive_thread, (void *) &recv_info);
    pthread_join(recv_thread, NULL);
    pthread_join(send_thread, NULL);
    fclose(fptr);
    return 0;
}
