/* HOW TO USE THIS PROGRAM:
** 
** This program is designed so that only two-letter commands need to be input to
** execute camera changes.
** e.g., cmd_status = runCommand("mi\r", file_descriptor, birger_output);
** where 'mi' is the command.
** 
** All camera settings are listed in the Canon EF 232 user manual.
** https://birger.com/products/lens_controller/manuals/canon_ef232/Canon%20EF-232%20Library%20User%20Manual%201.3.pdf
** 
** COMMANDS:
**  Aperture:
** 		in: initialize aperture motor; aperture will fully open
** 		da: print aperture information
** 		ma: move aperture to absolute position
** 		mc: move aperture to fully stopped down limit
** 		mn: move aperture incremental (mn2 moves the aperture by +2, not to +2)
** 		mo: move aperture to completely open
** 		pa: print aperture position
**  Focus:
** 		eh: set absolute lens focus position (0...0x3FFF)
** 		fa: move focus to absolute position
** 		fd: print focus distance range
** 		ff: fast focus
** 		fp: print the raw focus positions
** 		la: learn the focus range
** 		mf: move focus incremental (mf200 moves the focus by +200, not to +200; 
**                                  mf-200 increments down 200)
** 		mi: move focus to the infinity stop
** 		mz: move focus to the zero stop
** 		pf: print focus position
** 		sf: set the focus counter
**  Misc:
** 		bv: print the bootloader version
** 		de: dump EEPROM
** 		ds: prints distance stops
** 		dz: prints the zoom range
** 		ex: exit to the bootloader
** 		gs: echo current device and lens statuses
** 		hv: print the hardware version
** 		id: print basic lens identification (zoom and f-number)
** 		is: turn image stabilization off/on
** 		ll: library loaded check
** 		lp: lens presence
** 		ls: query lens for status immediately and print
** 		lv: print the library version string
** 		pl: lens power
** 		rm: set response modes
** 		se: temporarily set non-volatile (EEMPROM) byte
** 		sg: set GPIO
** 		sm: set special modes
** 		sn: print the device serial number
** 		sr: set spontaneous responses off/on
** 		vs: print the short version string
** 		we: write non-volatile parameters to EEPROM
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "lens_adapter.h"
#include "camera.h"
#include "commands.h"
#include "matrix.h"

/* Camera parameters global structure (defined in lens_adapter.h) */
struct camera_params all_camera_params = {
    .prev_focus_pos = 0,       // previous focus for distance to desired focus
    .focus_position = 0,       // current focus position
    .focus_inf = 0,            // default is not to set focus to infinity
    .aperture_steps = 0,       // number of aperture steps to shift by
    .max_aperture = 0,         // already be maximized from initCamera(), keep 0
    .current_aperture = 0,     // current aperture position
    .min_focus_pos = 0,        // current min focus position
    .max_focus_pos = 0,        // current max focus position
    .exposure_time = 100,      // current exposure time (800 msec is default)
    .change_exposure_bool = 0, // does user want to change exposure
    .gainfact = 1.0,           // current gain factor (1.0 is default)
    .change_gainfact_bool = 0, // does user want to change gain factor
    .begin_auto_focus = 1,     // auto-focus at beginning of camera's run
    .focus_mode = 0,           // camera begins in auto-focusing mode by default
    .start_focus_pos = 0,      // starting focus for auto-focusing search
    .end_focus_pos = 0,        // ending focus position also set below
    .focus_step = 5,           // by default, check every fifth focus position
    .photos_per_focus = 3,     // take 3 pictures per focus position by default
    .flux = 0.0,                 // first auto-focus max flux found will set this
};

char * birger_output, * buffer;
int file_descriptor, default_focus;
// global variables for solution to quadratic regression for auto-focusing
double a, b, c;

/* Helper function to print a 1D array.
** Input: The array to be printed.
** Output: None (void). Array is printed to terminal.
*/
void printArray(double * arr, int len) {
    printf("\n");

    for (int i = 0; i < len; i++) {
        printf("%lf\n", arr[i]);
    }
    
    printf("\n");
}

/* Helper function to print focus and flux data from the auto-focusing file.
** Input: The 1D integer flux and focus arrays.
** Output: None (void). Prints the arrays to the terminal for verification.
*/
void printFluxFocus(int * flux_arr, int * focus_arr, int num_elements) {
    printf("\n(*) Flux and focus data to compare with auto-focusing file:\n");

    for (int i = 0; i < num_elements; i++) {
        printf("%3d\t%6d\n", flux_arr[i], focus_arr[i]);
    }

    printf("\n");
}

/* Function to perform quadratic regressions during auto-focusing.
** Input: 1D arrays of flux and focus values, along with their length.
** Output: A flag indicating a successful solution via Gaussian elimination. 
** Calls Gaussian elimination function on the generated system of equations 
** to get a, b, c, the coefficients of the quadratic equation that fits the 
** flux data. 
*/
int quadRegression(int * flux_arr, int * focus_arr, int len) {
    if (verbose) {
        printFluxFocus(flux_arr, focus_arr, len);
    }

    // summation quantities
    double sumfocus = 0.0;
    double sumflux = 0.0;
    double sumfocus2 = 0.0;
    double sumfocus3 = 0.0;
    double sumfocus4 = 0.0;
    double sumfluxfocus = 0.0;
    double sumfocus2flux = 0.0;
    // vector to hold solution values (gaussianElimination will populate this)
    double solution[M]= {0};

    // find flux threshold
    double max_flux = -INFINITY;
    for (int i = 0; i < len; i++) {
        if (flux_arr[i] > max_flux) {
            max_flux = flux_arr[i];
        }
    }

    double min_flux = INFINITY; 
    for (int i = 0; i < len; i++) {
        if (flux_arr[i] < min_flux) {
            min_flux = flux_arr[i];
        }
    }

    double threshold = (max_flux + min_flux)/2.0;
    if (verbose) {
        printf("(*) Max flux is %f | min flux is %f\n", max_flux, min_flux);
        printf("(*) Flux threshold is %f\n\n", threshold);
    }

    // calculate the quantities for the normal equations
    double num_elements = 0.0;
    for (int i = 0; i < len; i++) {
        if (flux_arr[i] >= threshold) {
            if (verbose) {
                printf("Flux & focus > threshold: %d and %d\n", flux_arr[i], 
                                                                focus_arr[i]);
            }
            double f = focus_arr[i];
            sumfocus += f;
            sumflux += flux_arr[i];
            sumfocus2 += f*f;
            sumfocus3 += f*f*f;
            sumfocus4 += f*f*f*f;
            sumfluxfocus += f*flux_arr[i];
            sumfocus2flux += (f*f)*flux_arr[i];
            num_elements++;
        }
    }

    // create augmented matrix with this data to be solved
    double augmatrix[M][N] = {{sumfocus4, sumfocus3, sumfocus2, sumfocus2flux},
                              {sumfocus3, sumfocus2, sumfocus,  sumfluxfocus },
                              {sumfocus2, sumfocus,  num_elements, sumflux   }};
    if (verbose) {
        printf("(*) The original auto-focusing system of equations:\n");
        printMatrix(augmatrix);
    }
    
    // perform Gaussian elimination on this matrix
    if (gaussianElimination(augmatrix, solution) < 1) {
        printf("Unable to perform Gaussian elimination on the given matrix.\n");
        return -1;
    }

    if (verbose) {
        printf("\n (*) The upper triangular matrix:\n");
        printMatrix(augmatrix);
        printf("\n(*) The solution vector:");
        printArray(solution, M);
    }

    a = solution[0];
    b = solution[1];
    c = solution[2];

    return 1;
}

/* Function to initialize lens adapter and run commands for default settings.
** Input: Path to the file descriptor for the lens.
** Output: Flag indicating successful initialization of the lens.
*/
int initLensAdapter(char * path) {
    struct termios options;

    // open file descriptor with given path
    if ((file_descriptor = open(path, O_RDWR | O_NOCTTY)) < 0) {
        fprintf(stderr, "Error opening file descriptor to input path %s: %s.\n", 
                path, strerror(errno));
        return -1;
    }

    if (tcgetattr(file_descriptor, &options) < 0) {
        fprintf(stderr, "Error getting termios structure parameters: %s.\n", 
                strerror(errno));
        return -1;
    }

    // set input speed
    if (cfsetispeed(&options, B115200) < 0) {
        fprintf(stderr, "Unable to set input baud rate in termios struct "
                        "options to desired speed: %s.\n", strerror(errno));
        return -1;
    }

    // set output speed
    if (cfsetospeed(&options, B115200) < 0) {
        fprintf(stderr, "Unable to set output baud rate in termios struct "
                        "options to desired speed: %s.\n", strerror(errno));
        return -1;
    }                      

    // standard setting for DSP 1750:
    // 8-bit chars
    options.c_cflag = (options.c_cflag & ~CSIZE) | CS8;
    // ignore break signal
    options.c_iflag |= IGNBRK;
    // no signaling chars, no echo, no canonical processing
    options.c_lflag = 0;
    // no re-mapping, no delays
    options.c_oflag = 0;
    // The desired behavior is to listen for up to 99 chars (longer than the
    // length of a Birger response), or until the inter-character arrival time
    // exceeds 0.1 sec after receipt of first char. This effectively caps the
    // time it takes to return control flow to the rest of the program after a
    // lens command.
    // This scheme allows us to avoid usleeping for an arbitrary long time in
    // the loop, waiting for chars from Birger.
    // c.f. https://tldp.org/HOWTO/Serial-Programming-HOWTO/x115.html
    options.c_cc[VMIN] = 99;
    // (t = VTIME *0.1 s)
    options.c_cc[VTIME] = 1;
    // shut off xon/xoff control
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_cflag &= ~CSTOPB;
    options.c_oflag &= ~OPOST;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    // ignore modem controls, enable reading
    options.c_cflag |= (CLOCAL | CREAD);
    // shut off parity
    options.c_cflag &= ~(PARENB | PARODD);
    options.c_cflag &= ~CSTOPB;

    // turns off flow control maybe?
    // http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap11.html#tag_11_02_04
    // ^list of possible c_cflag options (doesn't include crtscts)
    // crtscts does not exist in termios.h doc
    options.c_cflag &= ~CRTSCTS;

    options.c_iflag |= ICRNL;

    // apply changes
    if (tcsetattr(file_descriptor, TCSANOW, &options) < 0) {
        fprintf(stderr, "Unable to apply changes to termios struct "
                        "options: %s.\n", strerror(errno));
        return -1;
    }

    // flush the buffer (in case of unclean shutdown)
    if (tcflush(file_descriptor, TCIOFLUSH) < 0) {
        fprintf(stderr, "Buffer failed to flush: %s.\n", strerror(errno));
        return -1;
    }

    // allocate space for returning values after running Birger commands
    birger_output = malloc(100);
    if (birger_output == NULL) {
        fprintf(stderr, "Error allocating output for Birger commands: %s.\n", 
                strerror(errno));
        return -1;
    }

    // set focus to 80 below infinity (hard-coded value  determined by testing)
    if (runCommand("la\r", file_descriptor, birger_output) == -1) {
        printf("Failed to learn current focus range.\n");
        return -1;
    }

    // OK to usleep here, because we DO expect this move to take an appreciable
    // amount of time, and we do this one time on init.
    usleep(1000000);

    // After learning focus range, try to move to infinity.
    if (runCommand("mi\r", file_descriptor, birger_output) == -1) {
        printf("Failed to move focus position to infinity.\n");
        return -1;
    }
    if (runCommand("mf -80\r", file_descriptor, birger_output) == -1) {
        printf("Failed to move the focus to the desired default position.\n");
        return -1;
    } else {
        printf("Focus moved to desired default position.\n");
    }

    printf("Focus at 80 counts below infinity:\n");
    if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
        printf("Failed to print the new focus position.\n");
        return -1;
    } 
    default_focus = all_camera_params.focus_position;
    printf("(*) Default focus value: %d\n", default_focus);

    // update auto-focusing values now that camera params struct is populated
    all_camera_params.start_focus_pos = all_camera_params.focus_position - 100;
    all_camera_params.end_focus_pos = all_camera_params.max_focus_pos - 25;

    // set aperture parameter to maximum
    all_camera_params.max_aperture = 1;

    // initialize the aperture motor
    if (runCommand("in\r", file_descriptor, birger_output) == -1) {
        printf("Failed to initialize the motor.\n");
        return -1;
    }

    // run the aperture maximization (fully open) command
    if (runCommand("mo\r", file_descriptor, birger_output) == -1) {
        printf("Setting the aperture to maximum fails.\n");
        return -1;
    }

    // print aperture position
    if (runCommand("pa\r", file_descriptor, birger_output) == -1) {
        printf("Failed to print the new aperture position.\n");
        return -1;
    } 

    // free up birger_output variable
    free(birger_output);
    return file_descriptor;
}

/* Function to navigate to the beginning of the auto-focusing range.
** Input: None.
** Output: A flag indicating successful movement to beginning of auto-focusing
** range or not.
*/
int beginAutoFocus() {
    char focus_str_cmd[10];

    // Always start AF runs by checking current focuser pos, to get right
    // delta to begin AF run.
    if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
        printf("Failed to print the new focus position.\n");
        return -1;
    }

    printf("\n> Beginning the auto-focus process...\n");
    printf("(*) Auto-focusing parameters: start = %d, stop = %d, step = %d.\n", 
           all_camera_params.start_focus_pos, all_camera_params.end_focus_pos,
           all_camera_params.focus_step);
    sprintf(focus_str_cmd, "mf %i\r", all_camera_params.start_focus_pos - 
                                      all_camera_params.focus_position);
    if (runCommand(focus_str_cmd, file_descriptor, birger_output) == -1) {
        printf("Failed to move focus to beginning of auto-focusing range.\n");
        return -1;
    } else {
        printf("Focus moved to beginning of auto-focusing range.\n");
    }

    // print focus to get new focus values and re-populate camera params struct
    if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
        printf("Failed to print the new focus position.\n");
        return -1;
    }

    return 1;
}

/* Function to navigate to the heuristically-determined default focus position
** (80 counts below infinity).
** Input: None.
** Output: A flag indicating movement to default focus position or not.
*/
int defaultFocusPosition() {
    char focus_str_cmd[10];

    // Always start by checking current focuser pos, to get correct delta.
    if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
        printf("Failed to print the new focus position.\n");
        return -1;
    }

    printf("> Moving to default focus position..\n");
    printf("(*) Default focus = %d, all_camera_params.focus_position = %d, "
           "default focus - focus position = %d\n",default_focus, 
           all_camera_params.focus_position, 
           default_focus - all_camera_params.focus_position);
    sprintf(focus_str_cmd, "mf %i\r", 
            default_focus - all_camera_params.focus_position);
    if (runCommand(focus_str_cmd, file_descriptor, birger_output) == -1) {
        printf("Failed to move the focus to the default position.\n");
        return -1;
    } else if (verbose) {
        printf("Focus moved to default focus - 80 counts below infinity.\n");
    }

    // print focus to get new focus values and re-populate camera params struct
    if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
        printf("Failed to print the new focus position.\n");
        return -1;
    } 

    return 1;
}

/* Function to shift by specified amount to next focus position.
** Input: The focus shift command, which includes how much we need to move by.
** Output: A flag indicating successful movement to the next focus position.
*/
int shiftFocus(char * cmd) {
    // shift to next focus position according to step size
    if (runCommand(cmd, file_descriptor, birger_output) == -1) {
        printf("Failed to move focus to next focus in auto-focusing range.\n");
        return -1;
    } else {
        printf("Focus moved to next focus position in auto-focusing range.\n");
    }

    // print the focus to get new focus values
    if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
        printf("Failed to print the new focus position.\n");
        return -1;
    } 

    return 1;
}

/* Function to use auto-focusing data to calculate the best focus position.
** Input: Number of focus positions stepped through and auto-focusing data file.
** Output: A flag indicating successful calculation of the optimal focus or not.
*/
int calculateOptimalFocus(int num_focus, char * auto_focus_file) {
    int focus = 0, flux = 0, ind = 0;
    FILE * af;
    char * af_line = NULL;
    size_t af_len = 0;
    int af_read, ret;
    double focus_max, flux_max, ydoubleprime;

    int * flux_y = calloc(num_focus, sizeof(int)); 
    if (flux_y == NULL) {
        fprintf(stderr, "Error allocating array for flux values: %s.\n", 
                strerror(errno));
        return -1;
    }

    int * focus_x = calloc(num_focus, sizeof(int));
    if (focus_x == NULL) {
        fprintf(stderr, "Error allocating array for focus values: %s.\n", 
                strerror(errno));
        return -1;
    }

    if ((af = fopen(auto_focus_file, "r")) == NULL) {
        fprintf(stderr, "Error opening auto-focusing file: %s.\n", 
                strerror(errno));
        return -1;
    }

    // read every line in the file
    printf("\n");
    while ((af_read = getline(&af_line, &af_len, af)) != -1) {
        sscanf(af_line, "%d\t%d\n", &flux, &focus);
        if (verbose) {
            printf("Auto-focusing data read in: %3d\t%5d\n", flux, focus);
        }
        flux_y[ind] = flux;
        focus_x[ind] = focus;
        ind++;
    }
    fflush(af);
    fclose(af);

    // perform quadratic regression on this data to find best fit curve
    if (quadRegression(flux_y, focus_x, num_focus) < 1) {
        printf("Unable to perform quad regression on auto-focusing data.\n");
        return -1;
    }

    free(flux_y);
    free(focus_x);

    // print the results of this regression
    if (verbose) {
        printf("Best-fit equation for auto-focusing data is: flux = %.3f*x^2 "
               "+ %.3f*x + %.3f, where x = focus.\n", a, b, c);
    }

    // find maximum of this curve and corresponding focus coordinate: 
    // yprime = 2ax + b -> set this to 0 and solve.
    focus_max = -b/(2*a);
    flux_max = a*(focus_max*focus_max) + b*focus_max + c;

    // take second derivative to ensure this is a maximum
    ydoubleprime = 2*a;

    if (ydoubleprime < 0) {
        // if second derivative at maximum is negative, it is truly a maximum
        printf("The largest flux found is %.3f.\n", flux_max);
        printf("Focus position corresponding to maximum brightness is %.3f. "
               "The nearest integer to this value is %.3f.\n", focus_max, 
               round(focus_max));
        focus_max = round(focus_max);
        return focus_max;
    } else {
        printf("Could not find focus corresponding to maximum flux.\n");
        // return something out of possible focus range to indicate error
        return -1000;
    }
}

/* Function to process and execute user commands for camera and lens settings. 
** Note: does not include adjustments to the blob-finding parameters and image 
** processing; this is done directly in commands.c in client handler function.
** Input: None.
** Output: None (void). Executes the commands and re-populates the camera params
** struct with the updated values.
*/
int adjustCameraHardware() {
    char focus_str_cmd[15]; 
    char aper_str_cmd[15]; 
    int focus_shift;
    int ret = 1;

    // if user set focus infinity command to true (1), execute this command and 
    // none of the other focus commands that would contradict this one
    if (all_camera_params.focus_inf == 1) {
        if (runCommand("mi\r", file_descriptor, birger_output) == -1) {
            printf("Failed to set focus to infinity.\n");
            ret = -1;
        } else {
            printf("Focus set to infinity.\n");
        }

        if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
            printf("Failed to print focus after setting to infinity.\n");
            ret = -1;
        } 
    } else {
        // calculate shift needed to get from current focus to user position
        focus_shift = all_camera_params.focus_position - 
                      all_camera_params.prev_focus_pos;
        if (verbose) {
            printf("Focus change to fulfill user cmd: %i\n", focus_shift);
        }

        if (focus_shift != 0) {
            sprintf(focus_str_cmd, "mf %i\r", focus_shift);

            // shift the focus 
            if (runCommand(focus_str_cmd, file_descriptor, birger_output) 
                == -1) {
                printf("Failed to move the focus to the desired position.\n");
                ret = -1;
            } else if (verbose) {
                printf("Focus moved to desired absolute position.\n");
            }

            // print focus position for confirmation
            if (runCommand("fp\r", file_descriptor, birger_output) == -1) {
                printf("Failed to print the new focus position.\n");
                ret = -1;
            }  
        }
    }

    // if the user wants to set the aperture to the maximum
    if (all_camera_params.max_aperture == 1) {
        // might as well change struct field here since we know what maximum 
        // aperture position is (don't have to get it with pa command)
        all_camera_params.current_aperture = 14; // Sigma 85mm f/1.4

        if (runCommand("mo\r", file_descriptor, birger_output) == -1) {
            printf("Setting the aperture to maximum fails.\n");
            ret = -1;
        } else {
            printf("Set aperture to maximum.\n");
        }
    } else {
        if (all_camera_params.aperture_steps != 0) {
            sprintf(aper_str_cmd, "mn%i\r", all_camera_params.aperture_steps);

            // perform the aperture command
            if (runCommand(aper_str_cmd, file_descriptor, birger_output) 
                == -1) {
                printf("Failed to adjust the aperture.\n");
                ret = -1;
            } else {
                printf("Adjusted the aperture successfully.\n");
            }

            // print new aperture position
            if (runCommand("pa\r", file_descriptor, birger_output) == -1) {
                printf("Failed to print the new aperture position.\n");
                ret = -1;
            }

            // now that command in aperture has been executed, set aperture 
            // steps field to 0 since it should not move again unless user sends
            // another command
            all_camera_params.aperture_steps = 0;  
        }
    }

    if (all_camera_params.change_exposure_bool) {
        // change boolean to 0 so exposure isn't adjusted again until user sends
        //  another command
        all_camera_params.change_exposure_bool = 0;
        ret = setExposureTime(all_camera_params.exposure_time);
    }

    if (all_camera_params.change_gainfact_bool) {
        // change boolean to 0 so gain isn't adjusted again until user sends
        // another command
        all_camera_params.change_gainfact_bool = 0;
        ret = setMonoAnalogGain(all_camera_params.gainfact);
    }

    return ret;
}

/* Function to execute built-in Birger commands.
** Input: The string identifier for the command, the file descriptor for lens
** adapter, and a string to print the Birger output to for verification.
** Output: Flag indicating successful execution of the command.
*/
int runCommand(const char * command, int file, char * return_str) {
    fd_set input, output;
    int status;

    FD_ZERO(&output);
    FD_SET(file, &output);

    if (!FD_ISSET(file, &output)) {
        fprintf(stderr, "File descriptor %d is not present in set "
                        "output: %s.\n", file, strerror(errno));
        return -1;
    }

    if (tcflush(file, TCIOFLUSH) < 0) {
        fprintf(stderr, "Error flushing non-transmitted output data, non-read "
                        "input data, or both: %s.\n", strerror(errno));
        return -1;
    }

    status = write(file, command, strlen(command));
    if (status < 0) {
        fprintf(stderr, "Unable to write cmd %s to file descriptor %d: %s.\n", 
                command, file, strerror(errno));
        return -1;
    }

    FD_ZERO(&input);
    FD_SET(file, &input);

    if (!FD_ISSET(file, &input)) {
        fprintf(stderr, "File descriptor %d is not present in set input: %s.\n",
                file, strerror(errno));
        return -1;
    }

    buffer = malloc(100);
    if (buffer == NULL) {
        fprintf(stderr, "Error allocating buffer in runCommand(): %s.\n", 
                strerror(errno));
        return -1;
    }

    buffer[0] = '\0';
    status = read(file, buffer, 99);
    if (status <= 0) {
        fprintf(stderr, "Error reading from file descriptor %d: %s.\n", file, 
                strerror(errno));
        return -1;
    }

    buffer[99] = '\0';
    buffer[status] = '\0';	
    if (strstr(buffer, "ERR") != NULL) {
        printf("Read returned error %s.\n", buffer);
        return -1;
    }
    
    // copy buffer over to return_str for printing to terminal
    return_str = malloc(100);
    if (return_str == NULL) {
        fprintf(stderr, "Error allocating return string for printing to "
                        "terminal in runCommand(): %s.\n", strerror(errno));
        return -1;
    }
    return_str[99] = '\0';
    strcpy(return_str, buffer);

    if (strcmp(command, "fp\r") == 0) {
        printf("%s\n", return_str);

        // parse the return_str for new focus range numbers
        sscanf(return_str, "fp\nOK\nfmin:%d  fmax:%d  current:%i %*s", 
               &all_camera_params.min_focus_pos, 
               &all_camera_params.max_focus_pos, 
               &all_camera_params.focus_position);
        if (verbose) {
            printf("in camera params, min focus pos is: %i\n", 
                   all_camera_params.min_focus_pos);
            printf("in camera params, max focus pos is: %i\n", 
                   all_camera_params.max_focus_pos);
            printf("in camera params, curr focus pos is: %i\n", 
                   all_camera_params.focus_position);
            printf("in camera params, prev focus pos was: %i\n", 
                   all_camera_params.prev_focus_pos);
        }

        // update previous focus position to current one 
        all_camera_params.prev_focus_pos = all_camera_params.focus_position;
        if (verbose) {
            printf("in camera params, prev focus pos is now: %i\n", 
                   all_camera_params.prev_focus_pos);
        }
    } else if (strcmp(command, "pa\r") == 0) {
        printf("%s\n", return_str);

        // store current aperture from return_str in all_camera_params struct
        if (strncmp(return_str, "pa\nOK\nDONE", 10) == 0) {
            sscanf(return_str, "pa\nOK\nDONE%*i,f%d", 
                   &all_camera_params.current_aperture);
        } else if (strncmp(return_str, "pa\nOK\n", 6) == 0) {
            sscanf(return_str, "pa\nOK\n%*i,f%d %*s", 
                   &all_camera_params.current_aperture);
        }

        printf("in camera params, curr aper is: %i\n", 
               all_camera_params.current_aperture);
    } else if (strncmp(command, "mf", 2) == 0) {
        printf("%s\n", return_str);
    }

    free(buffer);
    free(return_str);
    return 1;
}