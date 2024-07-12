#include <sys/types.h>  
#include <sys/socket.h> 
#include <string.h>     
#include <stdlib.h>     
#include <netinet/in.h>     
#include <stdio.h>          
#include <arpa/inet.h>
#include <ifaddrs.h> 
#include <netdb.h>         
#include <errno.h>      
#include <time.h>  
#include <math.h>  
#include <pthread.h>      
#include <signal.h>         
#include <ueye.h>
#include <getopt.h>           

#include "camera.h"
#include "astrometry.h"
#include "lens_adapter.h"
#include "commands.h"
#include "sc_listen.h"
#include "sc_send.h"


#pragma pack(push, 1)
/* Telemetry and camera settings structure */
struct telemetry {
    struct astrometry astrom;
    struct camera_params cam_settings; 
    struct blob_params current_blob_params;
};
/* User commands structure */
struct commands {
    double logodds;         // controls Astrometry false positives
    double latitude;        // user's latitude (radians)
    double longitude;       // user's longitude (degrees)
    double height;          // user's height above ellipsoid from GPS (WGS84)
    double exposure;        // user's desired camera exposure (msec)
    double timelimit;       // Astrometry tries to solve until this timeout
    float focus_pos;        // user's desired focus position (counts)
    int focus_mode;         // flag to enter auto-focusing mode
    int start_focus_pos;    // where to start the auto-focusing process
    int end_focus_pos;      // where to end the auto-focusing process
    int focus_step;         // granularity of auto-focusing checker
    int photos_per_focus;   // number of photos per auto-focusing position
    int set_focus_inf;      // whether or not to set the focus to infinity
    int aperture_steps;     // number of shifts (+/-) to reach desired aperture
    int set_max_aperture;   // whether or not to maximize aperture
    int make_hp;            // flag to make new static hp mask (20 = re-make)
    int use_hp;             // flag to use current static hp mask
    float blob_params[9];   // rest of blob-finding parameters
};
/* Struct for passing arguments to client thread(s) from main thread */
struct args {
    uintptr_t user_socket;
    socklen_t user_length;
    struct sockaddr_in user_address;
    char * user_IP;
};
#pragma pack(pop)

/* Pre-defined struct from getopt.h for additional long options */
static const struct option long_options[] = {
    { "valid",     no_argument,       NULL,  3  },
    { "number",    no_argument,       NULL,  4  },
    { "network",   no_argument,       NULL,  5  },
    { "verbose",   no_argument,       NULL, 'v' },
    { "help",      no_argument,       NULL, 'h' },
    { "camhandle", required_argument, NULL, 'c' },
    { "serial",    required_argument, NULL, 's' },
    { "port",      required_argument, NULL, 'p' },
    { NULL,        no_argument,       NULL,  0  },
};

struct commands all_cmds = {0};
struct telemetry all_data = {0};
int num_clients = 0;
int telemetry_sent = 0;
// if 1, then commanding is in use; if 0, then not
int command_lock = 0;
// flag for cancelling auto-focus mid-process
int cancelling_auto_focus = 0;
// assume non-verbose output
int verbose = 0;
void * camera_raw = NULL;
// if 0, then camera is not closing, so keep solving astrometry       
int shutting_down = 0;
// return values for terminating the threads
int astro_thread_ret, client_thread_ret;

/* Helper function to display the Star Camera terminal header.
** Input: None.
** Output: None.
*/
void printHeader() {
    printf("+---------------------------------------------------------+\n");
    printf("|     \tStar Camera command-line interface     \t\t  |\n");
    printf("+---------------------------------------------------------+\n");
}

/* Helper function to display inputs for the command-line interface.
** Input: None.
** Output: None.
*/
void displayUsage() {
    printHeader();
    printf("\nNAME:\n\tStar Camera command-line user interface.\n\nUSAGE:\n\t"
           "./commands -c [camera handle] -s [lens descriptor for serial "
           "adapter]\n\t\t   -p [socket port to create server on]\n\n"
           "DESCRIPTION:\n\tStar Camera command-line user interface. Runs the "
           "Star Camera to take\n\tpictures and solve for their pointing "
           "perpetually, while also accepting\n\tuser commands and "
           "transmitting telemetry. Specify a valid camera handle\n\tfrom 1 "
           "to 254, the known string descriptor for the serial port, and "
           "the\n\tport to establish a socket on.\n\nOPTIONS:\n\t-c, "
           "--camhandle\n\t\tCamera handle for the camera this program will "
           "control.\n\t\tRequired.\n\n\t-s, --serial\n\t\tLens descriptor. "
           "Required.\n\n\t-p, --port\n\t\tPort to bind this camera server "
           "socket to. Required.\n\n\t-v, --verbose\n\t\tIncrease output "
           "verbosity.\n\n\t--network\n\t\tShow the Star Camera computer IP "
           "address and the size of the\n\t\ttelemetry package.\n\n\t--number"
           "\n\t\tSee the current number of cameras connected to the computer."
           "\n\n\t--valid\n\t\tSee the valid combinations of the necessary "
           "input argument\n\t\t(handle + lens descriptor + socket port). "
           "While 0 to 65535 is\n\t\tthe range for valid TCP ports, you should "
           "specify one of the\n\t\tfollowing three, depending on which "
           "camera you're using:\n\n\t\t(1)\t8000\n\t\t(2)\t8001\n\t\t(3)"
           "\t8002\n\n");
}

/* Helper function for testing reception of user commands.
** Input: Nones.
** Output: None (void). Prints the most recent user commands to the terminal.
*/
void verifyUserCommands() {
    printf("\n+---------------------------------------------------------+\n");
    printf("| \t\tUser Commands \t\t\t\t  |\n");
    printf("|---------------------------------------------------------|\n");
    printf("|\tLogodds command: %f\t\t\t  |\n", all_cmds.logodds);
    printf("|\tLatitude | longitude: %f | %f\t  |\n", all_cmds.latitude, 
                                              all_cmds.longitude);
    printf("|\tExposure command in commands.c: %f\t  |\n", all_cmds.exposure);
    printf("|\tAstrometry timeout: %f\t\t\t  |\n", all_cmds.timelimit);
    printf("|\tFocus mode: %s\t\t\t  |\n", (all_cmds.focus_mode) ? 
           "Auto-focus" : "Normal focus");
    printf("|\tStart focus: %i, end focus: %i, focus step: %i\t  |\n", 
            all_cmds.start_focus_pos, all_cmds.end_focus_pos, 
            all_cmds.focus_step);
    printf("|\tPhotos per focus: %d\t\t\t\t  |\n", all_cmds.photos_per_focus);
    printf("|\tFocus position command: %f\t\t  |\n", all_cmds.focus_pos);
    printf("|\tSet focus to infinity bool command: %i\t\t  |\n", 
           all_cmds.set_focus_inf);
    printf("|\tAperture steps command: %i\t\t\t  |\n", all_cmds.aperture_steps);
    printf("|\tSet aperture max bool: %i\t\t\t  |\n", 
           all_cmds.set_max_aperture);
    printf("|\tMake static hp mask: %i, use static hp mask: %i\t  |\n", 
            all_cmds.make_hp, all_cmds.use_hp);
    printf("|\tBlob parameters: %f, %f, %f\t  |\n|\t\t\t %f, %f, "
           "%f\t  |\n|\t\t\t %f, %f, %f\t  |\n", 
            all_cmds.blob_params[0], all_cmds.blob_params[1], 
            all_cmds.blob_params[2], all_cmds.blob_params[3], 
            all_cmds.blob_params[4], all_cmds.blob_params[5],
            all_cmds.blob_params[6], all_cmds.blob_params[7], 
            all_cmds.blob_params[8]);
    printf("+---------------------------------------------------------+\n\n");
}

/* Helper function for testing transmission of telemetry.
** Input: None.
** Output: None (void). Prints the telemetry sent to the user to the terminal.
*/
void verifyTelemetryData() {
    printf("\n+---------------------------------------------------------+\n");
    printf("|\t\tTelemetry for User\t\t\t  |\n");
    printf("|---------------------------------------------------------|\n");
    printf("|\tCurrent rawtime: %f\t\t\t  |\n", all_data.astrom.rawtime);
    printf("|\tRA: %.15f\t\t\t\t  |\n", all_data.astrom.ra);
    printf("|\tDEC: %.15f\t\t\t\t  |\n", all_data.astrom.dec);
    printf("|\tFR: %.15f\t\t\t\t  |\n", all_data.astrom.fr);
    printf("|\tAZ: %.15f\t\t\t\t  |\n", all_data.astrom.az);
    printf("|\tALT: %.15f\t\t\t\t  |\n", all_data.astrom.alt);
    printf("|\tIR: %.15f\t\t\t\t  |\n", all_data.astrom.ir);
    printf("|\tPS: %f\t\t\t\t\t  |\n", all_data.astrom.ps);
    printf("|\tLogodds: %f\t\t\t\t  |\n", all_data.astrom.logodds);
    printf("|\tLatitude: %.15f\t\t\t  |\n", all_data.astrom.latitude);
    printf("|\tLongitude: %.15f\t\t\t  |\n", all_data.astrom.longitude);
    printf("|\tHeight: %f\t\t\t\t  |\n", all_data.astrom.hm);
    printf("+---------------------------------------------------------+\n\n");
}

/* Function devoted to taking pictures and solving astrometry while camera is 
** not in a state of shutting down.
** Input: None.
** Output: None (void). 
*/
void * updateAstrometry() {
    // solve astrometry perpetually when the camera is not shutting down
    while (!shutting_down) {
        if (doCameraAndAstrometry() < 1) {
            printf("Did not solve or timeout of Astrometry properly, or did not"
                   " auto-focus properly.\n");
        }
        if (all_trigger_params.trigger_mode == 0)
        // in the no-trigger mode we just sleep for 5 seconds
        // in triggered mode we don't want to sleep, triggers handle
        // all of the waiting time that we need with a sleep loop
        {
            printf("Sleeping between frames...\n");
            int timeBetweenFramesSec = 5;
            sleep(timeBetweenFramesSec);
        }
    }
    // when we are shutting down or exiting, close Astrometry engine and solver
    closeAstrometry();

    // terminate and exit thread
    astro_thread_ret = 1;
    pthread_exit(&astro_thread_ret);
}

/* Function for accepting newly connected clients and sending telemetry and 
** receiving their commands.
** Input: An args struct containing the client information.
** Output: None (void).
*/
void * processClient(void * for_client_thread) {
    // get the socket information for this user
    int socket = ((struct args *) for_client_thread)->user_socket;
    int length = ((struct args *) for_client_thread)->user_length;
    struct sockaddr_in address = ((struct args *) 
                                   for_client_thread)->user_address;
    char * ip_addr = ((struct args *) for_client_thread)->user_IP;
    int recv_status;

    // send data to this client perpetually and receive potential commands if 
    // user sends them
    while (1) {
        recv_status = recvfrom(socket, &all_cmds, sizeof(struct commands), 0, 
                              (struct sockaddr *) &address, &length); 
        if (recv_status == -1) {
            printf("User %s did not send any commands. Send telemetry and "
                   "camera settings back anyway.\n", ip_addr);
        } else {
            // if another client sends commands at the same time, wait until 
            // they are done
            while (command_lock) {
                usleep(100000);
            }
            // now it's this client's turn to execute commands (lock)
            command_lock = 1;
            printf("> User %s sent commands. Executing...\n", ip_addr);
            if (verbose) {
                verifyUserCommands();
            }

            // some constants for solving Astrometry
            all_astro_params.logodds = all_cmds.logodds;
            all_astro_params.latitude = all_cmds.latitude;
            all_astro_params.longitude = all_cmds.longitude;
            all_astro_params.hm = all_cmds.height;
            all_astro_params.timelimit = all_cmds.timelimit;

            // update blob-finding parameters (see camera.h for documentation)
            all_blob_params.make_static_hp_mask = all_cmds.make_hp;            
            all_blob_params.use_static_hp_mask = all_cmds.use_hp;                 

            if (all_cmds.blob_params[0] >= 0) {
                all_blob_params.spike_limit = all_cmds.blob_params[0];        
            } 
            
            all_blob_params.dynamic_hot_pixels = all_cmds.blob_params[1];    
        
            if (all_cmds.blob_params[2] >= 0) {
                all_blob_params.r_smooth = all_cmds.blob_params[2];      
            }

            all_blob_params.high_pass_filter = all_cmds.blob_params[3];   
            
            if (all_cmds.blob_params[4] >= 0) {
                all_blob_params.r_high_pass_filter = all_cmds.blob_params[4]; 
            } 
            
            if (all_cmds.blob_params[5] >= 0) {
                all_blob_params.centroid_search_border = all_cmds.blob_params[5];
            } 

            all_blob_params.filter_return_image = all_cmds.blob_params[6];  

            if (all_cmds.blob_params[7] >= 0) {
                all_blob_params.n_sigma = all_cmds.blob_params[7];     
            } 
            
            if (all_cmds.blob_params[8] >= 0) {
                all_blob_params.unique_star_spacing = all_cmds.blob_params[8];
            } 

            if (!all_cmds.focus_mode && all_camera_params.focus_mode) {
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
            if (all_cmds.focus_mode) {
                all_camera_params.begin_auto_focus = 1;
            }
            all_camera_params.focus_mode = all_cmds.focus_mode;
            all_camera_params.start_focus_pos = all_cmds.start_focus_pos;
            all_camera_params.end_focus_pos = all_cmds.end_focus_pos;
            all_camera_params.focus_step = all_cmds.focus_step;
            all_camera_params.photos_per_focus = all_cmds.photos_per_focus;

            if (!all_camera_params.focus_mode && !cancelling_auto_focus) {
                // if user adjusted exposure, set exposure to their value
                if (ceil(all_cmds.exposure) != 
                    ceil(all_camera_params.exposure_time)) {
                    // update value in camera params struct as well
                    all_camera_params.exposure_time = all_cmds.exposure;
                    all_camera_params.change_exposure_bool = 1;
                }

                // contradictory commands between setting focus to inf and 
                // adjusting it to a different position are handled in 
                // adjustCameraHardware()
                all_camera_params.focus_inf = all_cmds.set_focus_inf;
                // if user wants to change focus, change focus in camera params
                if (all_cmds.focus_pos != -1) {
                    all_camera_params.focus_position = all_cmds.focus_pos; 
                } 

                // update camera params struct with user commands
                all_camera_params.max_aperture = all_cmds.set_max_aperture;
                all_camera_params.aperture_steps = all_cmds.aperture_steps;

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

            // allow other clients to execute commands (unlock)
            command_lock = 0; 
        }
        telemetry_sent = 0;

        while (!send_data) {
            usleep(100000);
        }

        // compile telemetry and transmit back to use
        memcpy(&all_data.astrom, &all_astro_params, 
                sizeof(all_astro_params));
        memcpy(&all_data.cam_settings, &all_camera_params, 
                sizeof(all_camera_params));
        memcpy(&all_data.current_blob_params, &all_blob_params, 
                sizeof(all_blob_params));

        if (send(socket, &all_data, sizeof(struct telemetry), 
                 MSG_NOSIGNAL) <= 0) {
            printf("Client dropped the connection.\n");
            break;
        } 


        if (send(socket, camera_raw, CAMERA_WIDTH*CAMERA_HEIGHT, 
                 MSG_NOSIGNAL) <= 0) {
            printf("Client dropped the connection.\n");
            break;
        }

        telemetry_sent = 1;
        
        if (verbose) {
            printf("Telemetry and image bytes sent back to user.\n"); 
            verifyTelemetryData();
        }
    }
    // clean up socket when the connection is done
    close(socket);
    num_clients--;

    // in case user exits in middle of two send() calls above, we don't want to
    // wait forever in auto-focusing
    telemetry_sent = 1;

    // free(for_client_thread);
    client_thread_ret = 1;
    pthread_exit(&client_thread_ret);
}

/* Polarity crisis: am I north or south? */
/* Right now fc2 == south */
static int am_i_SC1(void)
{
    char buffer[4];

    if (gethostname(buffer, 3) == -1 && errno != ENAMETOOLONG) {
      perror("System: Unable to get hostname");
    }

    return ((buffer[0] == 's') && (buffer[1] == 'c') && (buffer[2] == '1')) ? 1 : 0;
}


/* Driver function for Star Camera operation.
** Input: Number of command-line arguments passed and an array of those argu-
** ments.
** Output: Flag indicating successful Star Camera operation or an error -> exit.
*/
int main(int argc, char * argv[]) {
    // signal handling (e.g. ctrl+c exception)
    signal(SIGHUP, clean);
    signal(SIGINT, clean);
    signal(SIGTERM, clean);
    signal(SIGPIPE, SIG_IGN);

    // setup the MCP monitoring thread here
    pthread_t listen_fc1;
    sprintf(fc1Socket_listen.ipAddr,"%s",FC1_IP_ADDR);
    sprintf(fc1Socket_listen.port,"%s",SC_COMMAND_PORT_FC1);
    fc1Socket_listen.image_solutions = NULL;
    fc1Socket_listen.camera_params = NULL;
    fc1Socket_listen.camera_trigger = NULL;
    fc1Socket_listen.camera_commands = &FC1_in;

    pthread_t listen_fc2;
    sprintf(fc2Socket_listen.ipAddr,"%s",FC2_IP_ADDR);
    sprintf(fc2Socket_listen.port,"%s",SC_COMMAND_PORT_FC2);
    fc2Socket_listen.image_solutions = NULL;
    fc2Socket_listen.camera_params = NULL;
    fc2Socket_listen.camera_trigger = NULL;
    fc2Socket_listen.camera_commands = &FC2_in;

    // setup the software trigger thread here
    pthread_t listen_fc1_trigger;
    sprintf(fc1Socket_trigger.ipAddr,"%s",FC1_IP_ADDR);
    sprintf(fc1Socket_trigger.port,"%s",SC_TRIGGER_PORT_FC1);
    fc1Socket_trigger.image_solutions = NULL;
    fc1Socket_trigger.camera_params = NULL;
    fc1Socket_trigger.camera_trigger = &FC1_trigger;
    fc1Socket_trigger.camera_commands = NULL;

    pthread_t listen_fc2_trigger;
    sprintf(fc2Socket_trigger.ipAddr,"%s",FC2_IP_ADDR);
    sprintf(fc2Socket_trigger.port,"%s",SC_TRIGGER_PORT_FC2);
    fc2Socket_trigger.image_solutions = NULL;
    fc2Socket_trigger.camera_params = NULL;
    fc2Socket_trigger.camera_trigger = &FC2_trigger;
    fc2Socket_trigger.camera_commands = NULL;


    // setups MCP data return threads here

    // TODO(Ian): add the logic to change things like "SC1_RECEIVE_PARAM_PORT"
    // to instead return a port number based on southiam style whoami
    static int i_am_SC1;
    i_am_SC1 = am_i_SC1();
    if (i_am_SC1)
    {
        printf("I am SC1 and am setting up comms to FC1 as such\n");
        // set up the parameter comm socket on SC1 port
        sprintf(fc1_return_socket.ipAddr, "%s", FC1_IP_ADDR);
        sprintf(fc1_return_socket.port,"%s", SC1_RECEIVE_PARAM_PORT);
        // set up the image data comm socket on SC1 port
        sprintf(fc1_image_socket.ipAddr, "%s", FC1_IP_ADDR);
        sprintf(fc1_image_socket.port, "%s", SC1_RECEIVE_SOLVE_PORT);
    } else if (!i_am_SC1)
    {
        printf("I am SC2 and am setting up comms to FC1 as such\n");
        // set up the parameter comm socket on SC2 port
        sprintf(fc1_return_socket.ipAddr, "%s", FC1_IP_ADDR);
        sprintf(fc1_return_socket.port,"%s", SC2_RECEIVE_PARAM_PORT);
        // set up the image data comm socket on SC2 port
        sprintf(fc1_image_socket.ipAddr, "%s", FC1_IP_ADDR);
        sprintf(fc1_image_socket.port, "%s", SC2_RECEIVE_SOLVE_PORT);
    }
    pthread_t params_fc1;
    fc1_return_socket.image_solutions = NULL;
    fc1_return_socket.camera_params = &FC1_return;
    fc1_return_socket.camera_trigger = NULL;
    fc1_return_socket.camera_commands = NULL;
    pthread_t images_fc1;
    fc1_image_socket.image_solutions = &FC1_astro;
    fc1_image_socket.camera_params = NULL;
    fc1_image_socket.camera_trigger = NULL;
    fc1_image_socket.camera_commands = NULL;

    if (i_am_SC1)
    {
        printf("I am SC1 and am setting up comms to FC2 as such\n");
        // set up the parameter comm socket on SC1 port
        sprintf(fc2_return_socket.ipAddr, "%s", FC2_IP_ADDR);
        sprintf(fc2_return_socket.port,"%s", SC1_RECEIVE_PARAM_PORT);
        // set up the image data comm socket on SC1 port
        sprintf(fc2_image_socket.ipAddr, "%s", FC2_IP_ADDR);
        sprintf(fc2_image_socket.port, "%s", SC1_RECEIVE_SOLVE_PORT);
    } else if (!i_am_SC1)
    {
        printf("I am SC2 and am setting up comms to FC2 as such\n");
        // set up the parameter comm socket on SC2 port
        sprintf(fc2_return_socket.ipAddr, "%s", FC2_IP_ADDR);
        sprintf(fc2_return_socket.port,"%s", SC2_RECEIVE_PARAM_PORT);
        // set up the image data comm socket on SC2 port
        sprintf(fc2_image_socket.ipAddr, "%s", FC2_IP_ADDR);
        sprintf(fc2_image_socket.port, "%s", SC2_RECEIVE_SOLVE_PORT);
    }
    pthread_t params_fc2;
    fc2_return_socket.image_solutions = NULL;
    fc2_return_socket.camera_params = &FC2_return;
    fc2_return_socket.camera_trigger = NULL;
    fc2_return_socket.camera_commands = NULL;
    pthread_t images_fc2;
    fc2_image_socket.image_solutions = &FC2_astro;
    fc2_image_socket.camera_params = NULL;
    fc2_image_socket.camera_trigger = NULL;
    fc2_image_socket.camera_commands = NULL;

    int opt;                         // parsing command-line options
    int long_index = 0;              // for tracking which option we're at
    char * port = NULL;              // port to bind socket to
    char * lens_desc = NULL;         // file descriptor for Birger lens adapter
    char * handle = NULL;            // will be passed to camera_handle
    int test_handle, test_port;      // for testing the values of user input
    int sockfd;                      // to create socket
    int newsockfd;                   // to accept new connection(s)
    struct sockaddr_in serv_addr;    // server receives on this address
    struct sockaddr_in client_addr;  // server sends to client on this address
    struct timeval read_timeout;     // timeout options for server socket 
    int client_addr_len;             // length of client addresses
    pthread_t client_thread_id;      // thread ID for new clients        
    pthread_t astro_thread_id;       // thread ID for Astrometry thread
    struct args * client_args;       // arguments to pass to clients
    int * astro_ptr = NULL;          // ptr for returning from Astrometry thread
    int * client_ptr = NULL;         // ptr for returning from client thread
    int any_clients = 0;             // if a client every connected during run
    int ret;                         // return status of main()

    // parse command-line options
    while ((opt = getopt_long(argc, argv, ":c:s:p:vh?", long_options, 
                              &long_index)) != -1) {
        switch (opt) {
            // we will check the essential arguments after
            case 'c':
                handle = optarg;
                break;
            case 's':
                lens_desc = optarg;
                break;
            case 'p':
                port = optarg;
                break;
            case 'v':
                // turn on verbose output
                verbose = 1;
                break;
            case 'h':
                displayUsage();
                return 1;
            case 3:
                // display valid handle, serial port, and port combinations
                printHeader();
                printf("\nValid <handle> <serial port> <socket port> argument "
                       "combinations are:\n");
                printf("(1)\t12\t/dev/ttyLens12port8000\t8000\n");
                break;
            case 4:
                // number of cameras connected to PC
                printHeader();
                int num_cams;

                if (is_GetNumberOfCameras(&num_cams) != IS_SUCCESS) {
                    printf("Cannot get # of cameras connected to computer.\n");
                    return 0;
                } 
                printf("Number of cameras connected to computer: %d.\n",
                       num_cams);

                break;
            case 5:
                // display network infomation
                printHeader();
                printf("\nEnter the address in the 'inet addr' field into\nthe " 
                       "Star Camera application to connect:\n\n");
                system("/sbin/ifconfig enp4s0");
                printf("Size of data packet that gets sent to user: "
                       "%lu bytes\n", sizeof(all_data));
                break;
            case ':':
                // missing arguments (but option itself is given)
                printHeader();
                fprintf(stderr, "Option '-%c' requires an argument.\n", optopt);
                return 0;
            case '?':
                printHeader();
                // invalid arguments
                fprintf(stderr, "Option '-%c' is invalid.\n", optopt);
                return 0;
        }
    }

    // make sure we have all the essential arguments
    if (handle == NULL) {
        printHeader();
        printf("\nMissing camera handle. Run ./commands --help for details.\n");
        return 0;
    }

    if (lens_desc == NULL) {
        printHeader();
        printf("\nMissing serial port. Run ./commands --help for details.\n");
        return 0;
    }

    if (port == NULL) {
        printHeader();
        printf("\nMissing socket port. Run ./commands --help for details.\n");
        return 0;
    }

    printHeader();
    printf("|     \tCamera handle: %s     \t\t\t\t  |\n", handle);
    printf("|     \tSerial Port: %s     \t  |\n", lens_desc);
    printf("|     \tSocket Port: %s     \t\t\t\t  |\n", port);
    printf("+---------------------------------------------------------+\n");

    // if the arguments exist, check their values are valid
    camera_handle = atoi(handle);
    if (camera_handle > 254 || camera_handle < 1) {
        printf("Invalid camera handle. Choose one in the range 1-254.\n");
        return 0;
    }

    if (atoi(port) > 65535 || atoi(port) < 0) {
        printf("Invalid TCP socket port. Choose one in the range 0-65535.\n");
        return 0;
    }

    // then check that the socket port and camera handle in the serial port are
    // a valid combination
    if (sscanf(lens_desc, "/dev/ttyLens%dport%d", &test_handle, &test_port) 
        < 2) {
        printf("Invalid lens descriptor. Run ./commands --help for details.\n");
    }

    // one valid sequence is handle = 12 and port = 8000
    if (test_port != atoi(port)) {
        printf("Socket port %s does not match the one listed in the lens"
               "descriptor: %s.\n", port, lens_desc);
        return 0;
    }

    if (test_handle == 12 && atoi(port) != 8000) {
        printf("Camera handle 12 corresponds to socket port of 8000, not %s.\n", 
               port);
        return 0;
    }

    // create server socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Error creating Star Camera server socket: %s.\n", 
                strerror(errno));
        // the program did not successfully run
        exit(EXIT_FAILURE);
    }

    // initialize the server socket addresses
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(port)); 

    // bind the server socket with the server address and port
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        fprintf(stderr, "Error binding Star Camera server socket to Star Camera"
                        " address and port: %s. Try again in a few seconds - "
                        "resources may not be freed if last run just ended.\n", 
                strerror(errno));
        // the program did not successfully run
        exit(EXIT_FAILURE);
    }

    // listen for connections from clients (maximum is 5 waiting)
    if (listen(sockfd, 5) < 0) {
        fprintf(stderr, "Star Camera server error listening for clients: %s.\n", 
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // make server socket non-blocking (times out after a certain time)
    read_timeout.tv_sec = 3;
    read_timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, 
                   sizeof(read_timeout)) < 0) {
        fprintf(stderr, "Error setting Star Camera server timeout: %s.\n", 
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // allow address to be reused (in case of shutdown with client still
    // connected)
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, 
                   sizeof(int)) < 0) {
        fprintf(stderr, "Error setting server reuseaddrr option: %s.\n", 
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // initialize the camera with input ID
    if (initCamera() < 0) {
        printf("Could not initialize camera due to above error. Could be that "
               "you specified a handle for a camera already in use.\n");
        // if camera was already initialized, close it before exiting
        if (camera_handle > 0) {
            closeCamera();
        }
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // initialize the lens adapter
    if (initLensAdapter(lens_desc) < 0) {
        printf("Could not initialize lens adapter due to above error.\n");
        // closeCamera();
        // close(sockfd);
        // exit(EXIT_FAILURE);
    }

    // create a thread separate from all client thread(s) to solve Astrometry 
    if (pthread_create(&astro_thread_id, NULL, updateAstrometry, NULL) != 0) {
        fprintf(stderr, "Error creating Astrometry thread: %s.\n", 
                strerror(errno));
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Create the listening threads for MCP
    pthread_create(&listen_fc1, NULL, listen_thread, (void *) &fc1Socket_listen);
    pthread_create(&listen_fc2, NULL, listen_thread, (void *) &fc2Socket_listen);

    // Create the trigger listening threads
    pthread_create(&listen_fc1_trigger, NULL, trigger_thread, (void *) &fc1Socket_trigger);
    pthread_create(&listen_fc2_trigger, NULL, trigger_thread, (void *) &fc2Socket_trigger);

    // Create the talking threads for MCP
    pthread_create(&images_fc1, NULL, astrometry_data_thread, (void *) &fc1_image_socket);
    pthread_create(&params_fc1, NULL, parameter_data_thread, (void *) &fc1_return_socket); 
    pthread_create(&images_fc2, NULL, astrometry_data_thread, (void *) &fc2_image_socket);
    pthread_create(&params_fc2, NULL, parameter_data_thread, (void *) &fc2_return_socket); 

    // loop forever, accepting new clients
    client_addr_len = sizeof(struct sockaddr_in); 
    while ((!shutting_down) && (newsockfd = accept(sockfd, (struct sockaddr *) 
                                                           &client_addr, 
                                                           &client_addr_len))) {
        // parent process waiting to accept a new connection
        printf("\n+---------------------------------------------------------+\n");
        printf("| Server waiting for new client, %d already connected\t  |\n",
           num_clients);
        printf("+---------------------------------------------------------+\n");

        // store length of client that has connected (if any)
        client_addr_len = sizeof(client_addr);
        if (newsockfd == -1) {
            printf("New client did not connect.\n");
        } else {
            any_clients = 1;

            // user did connect so process their info for their client thread
            printf("Client %s connected.\n", inet_ntoa(client_addr.sin_addr)); 

            client_args = (struct args *) malloc(sizeof(struct args));
            if (client_args == NULL) {
                fprintf(stderr, "Error creating struct for new client: %s.\n", 
                        strerror(errno));
                continue;
            }

            client_args->user_socket = newsockfd;
            client_args->user_length = client_addr_len;
            client_args->user_address = client_addr;
            client_args->user_IP = inet_ntoa(client_addr.sin_addr);

            // create new thread for this new client
            if (pthread_create(&client_thread_id, NULL, processClient, 
                              (void *) client_args) < 0) {
                fprintf(stderr, "Error creating thread for new client: %s.\n", 
                        strerror(errno));
            }

            num_clients++;
        }
    }

    // Join the listening threads now that everything is dying
    pthread_join(listen_fc1, NULL);
    pthread_join(listen_fc2, NULL);
    pthread_join(listen_fc1_trigger, NULL);
    pthread_join(listen_fc2_trigger, NULL);
    pthread_join(images_fc1, NULL);
    pthread_join(images_fc2, NULL);
    pthread_join(params_fc1, NULL);
    pthread_join(params_fc2, NULL);

    // join threads once the Astrometry thread has closed and terminated
    pthread_join(astro_thread_id, (void **) &(astro_ptr));
    if (any_clients) {
        pthread_join(client_thread_id, (void **) &(client_ptr));
        free(client_args);
    }

    closeCamera();
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    if (*astro_ptr == 1) {
        printf("\nSuccessfully exited Astrometry.\n");
        ret = 1;
    } else {
        printf("\nDid not return successfully from Astrometry thread.\n");
        ret = 0;
    }

    if (any_clients) {
        if (*client_ptr == 1) {
            printf("\nSuccessfully exited client thread.\n");
            ret = 1;
        } else {
            printf("\nDid not return successfully from client thread.\n");
            ret = 0;
        }
    }

    return ret;
}
