#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <sofa.h>

#include "camera.h"
#include "astrometry.h"
#include "commands.h"
#include "lens_adapter.h"
#include "matrix.h"
#include "sc_data_structures.h"
#include "convolve.h"
#include "fits_utils.h"
#include "timer.h"


#define AF_ALGORITHM_NEW

void merge(double A[], int p, int q, int r, double X[],double Y[]);
void part(double A[], int p, int r, double X[], double Y[]);

#ifdef IDS_PEAK
#include <ids_peak_comfort_c/ids_peak_comfort_c.h>
peak_camera_handle hCam = PEAK_INVALID_HANDLE;

// We include a default metadata struct here to help with initialization
// elsewhere. This could also be handled in a template FITS with using the
// FITSIO interface to template files.
struct fits_metadata_t default_metadata = {

    // Capture data

    .origin = "blastcam",
    .instrume = "TIMcam",
    .telescop = "Sigma 85mm f/1.4 DG HSM ART",
    .observat = "TIM",
    .observer = "starcam",
    .filename = "",
    .date = "1970-00-00T00:00:00",
    .utcsec = 0,
    .utcusec = 0,
    .filter = "B+W 091 (630nm)",
    .ccdtemp = 0.0,
    .focus = 0,
    .aperture = 14,
    .exptime = 0.1,
    .bunit = "ADU",

    // Compression settings

    .fzalgor = "RICE_1",
    .fztile = "ROW",

    // Sensor settings

    .detector = "iDS U3-31N0CP-M-GL Rev. 2.2",
    .sensorid = 0,
    .bitdepth = 12,
    .pixscal1 = 6.63,
    .pixscal2 = 6.63,
    .pixsize1 = 2.74,
    .pixsize2 = 2.74,
    .darkcur = 1.38,
    .rdnoise1 = 2.37,
    .ccdbin1 = 1,
    .ccdbin2 = 1,
    .pixelclk = 99.0,
    .framerte = 1.0,
    .gainfact = 1.0,
    .trigdlay = 0.0,
    .bloffset = 0.0,
    .autogain = 0,
    .autoexp = 0,
    .autoblk = 0
};
#else
#include <ueye.h>
// 1-254 are possible IDs. Command-line argument from user with ./commands
HIDS camera_handle;
// breaking convention of using underscores for struct names because this is how
// iDS does it in their documentation
IMAGE_FILE_PARAMS ImageFileParams;
// same with sensorInfo struct
SENSORINFO sensorInfo;
#endif

// global variables
int image_solved[2] = {0};
int send_data = 0;
int taking_image = 0;
int default_focus_photos = 3;
int buffer_num, mem_id;
uint16_t * memory, * mem_starting_ptr; //we want raw bytes
unsigned char * mask;

struct timeval metadataTv;

double deltaT = 0.0;
struct timespec tstart = {0,0};
struct timespec tend = {0,0};

uint16_t unpacked_image[CAMERA_NUM_PX] = {0};
// for printing camera errors
const char * cam_error;
// 'curr' = current, 'pc' = pixel clock, 'fps' = frames per sec, 
// 'ag' = auto gain, 'bl' = black level
double curr_exposure, curr_ag, curr_shutter, auto_fr;
int curr_pc, curr_color_mode, curr_ext_trig, curr_trig_delay, curr_master_gain;
int curr_red_gain, curr_green_gain, curr_blue_gain, curr_gamma, curr_gain_boost;
unsigned int curr_timeout;
int bl_offset, bl_mode;
int prev_dynamic_hp;

// For realtime contrast AF
// Have to declare here or we'd run out of stack space and mysterious-looking SEGFAULT
float imageFloatIn[CAMERA_NUM_PX] = {0.0};
float imageFloatOut[CAMERA_NUM_PX] = {0.0};
float sobelResult[CAMERA_NUM_PX] = {0.0};

/* Blob parameters global structure (defined in camera.h) */
struct blob_params all_blob_params = {
    .spike_limit = 3,             
    .dynamic_hot_pixels = 1,       
    // .r_smooth = 2,   
    .r_smooth = 1,              
    .high_pass_filter = 1,         
    .r_high_pass_filter = 10,     
    .centroid_search_border = 1,  
    .filter_return_image = 0,     
    .n_sigma = 10.0,               
    .unique_star_spacing = 15,    
    .make_static_hp_mask = 0,     
    .use_static_hp_mask = 1,       
};

/* Trigger parameters global structure (defined in camera.h) */
struct trigger_params all_trigger_params = {
    .trigger_timeout_us = 100, // 100 µs between checks
    .trigger = 0, // not starting off triggered
    .trigger_mode = 0, // default to 
};

enum solveState_t solveState = UNINIT;


/* Helper function to determine if a year is a leap year (2020 is a leap year).
** Input: The year.
** Output: A flag indicating the input year is a leap year or not.
**/
int isLeapYear(int year)
{
    if (year % 4 != 0) {
        return false;
    } else if (year % 400 == 0) {
        return true;
    } else if (year % 100 == 0) {
        return false;
    } else {
        return true;
    }
}

/* Helper function to test blob-finding parameters.
** Input: None.
** Output: None (void). Prints current blob-finding parameters to the terminal.
**/
void verifyBlobParams(void)
{
    printf("\n+---------------------------------------------------------+\n");
    printf("|\t\tBlob-finding parameters\t\t\t  |\n");
    printf("|---------------------------------------------------------|\n");
    printf("|\tall_blob_params.spike_limit is: %d\t\t  |\n", 
           all_blob_params.spike_limit);
    printf("|\tall_blob_params.dynamic_hot_pixels is: %d\t  |\n", 
           all_blob_params.dynamic_hot_pixels);
    printf("|\tall_blob_params.centroid_search_border is: %d\t  |\n", 
           all_blob_params.centroid_search_border);
    printf("|\tall_blob_params.high_pass_filter is: %d\t\t  |\n", 
           all_blob_params.high_pass_filter);
    printf("|\tall_blob_params.r_smooth is: %d\t\t\t  |\n", 
           all_blob_params.r_smooth);
    printf("|\tall_blob_params.filter_return_image is: %d\t  |\n", 
           all_blob_params.filter_return_image);
    printf("|\tall_blob_params.r_high_pass_filter is: %d\t  |\n", 
           all_blob_params.r_high_pass_filter);
    printf("|\tall_blob_params.n_sigma is: %.2f\t\t  |\n", 
           all_blob_params.n_sigma);
    printf("|\tall_blob_params.unique_star_spacing is: %d\t  |\n", 
           all_blob_params.unique_star_spacing);
    printf("|\tall_blob_params.make_static_hp_mask is: %i\t  |\n", 
           all_blob_params.make_static_hp_mask);
    printf("|\tall_blob_params.use_static_hp_mask is: %i\t  |\n", 
           all_blob_params.use_static_hp_mask);
    printf("+---------------------------------------------------------+\n\n");
}


/**
 * @brief function to take 16 bit pixel values which are filled with 12 bits and 
 * mask out the last 4 bits to ensure they are zero and not random garbage
 * @param packed pointer to array of pixel values from camera transfer
 * @param unpacked pointer to destination array for use by rest of code
 * @param num_pixels total number of pixels in image
 */
void unpack_mono12(uint16_t* packed, uint16_t* unpacked, int num_pixels)
{
    for (int i = 0; i < num_pixels; i++) {
        unpacked[i] = packed[i] & 0x0FFF; // this masks the last four pixels
    }
}


/* Helper function to print camera errors.
** Input: None.
** Output: The error from the camera.
*/
#ifndef IDS_PEAK
const char * printCameraError()
{
    char * last_error_str;
    int last_err = 0;

    is_GetError(camera_handle, &last_err, &last_error_str);
    return last_error_str;
}
#endif

#ifdef IDS_PEAK
/**
 * @brief Helper function to check return values of peak library calls and print
 * errors.
 * 
 * @param checkStatus 
 * @return peak_bool 
 */
peak_bool checkForSuccess(peak_status checkStatus)
{
    if (PEAK_ERROR(checkStatus)) {
        peak_status lastErrorCode = PEAK_STATUS_SUCCESS;
        size_t lastErrorMessageSize = 0;
    
        // Get size of error message
        peak_status status = peak_Library_GetLastError(&lastErrorCode, NULL, &lastErrorMessageSize);
        if (PEAK_ERROR(status)) {
            // Something went wrong getting the last error!
            printf("Last-Error: Getting last error code failed! Status: %#06x\n", status);
            return PEAK_FALSE;
        }
    
        if (checkStatus != lastErrorCode) {
            // Another error occured in the meantime. Proceed with the last error.
            printf("Last-Error: Another error occured in the meantime!\n");
        }
    
        // Allocate and zero-initialize the char array for the error message
        char* lastErrorMessage = (char*)calloc((lastErrorMessageSize) / sizeof(char),
            sizeof(char));
        if (lastErrorMessage == NULL) {
            // Cannot allocate lastErrorMessage. Most likely not enough Memory.
            printf("Last-Error: Failed to allocate memory for the error message!\n");
            free(lastErrorMessage);
            return PEAK_FALSE;
        }
    
        // Get the error message
        status = peak_Library_GetLastError(&lastErrorCode, lastErrorMessage, &lastErrorMessageSize);
        if (PEAK_ERROR(status)) {
            // Unable to get error message. This shouldn't ever happen.
            printf("Last-Error: Getting last error message failed! Status: %#06x; Last error code: %#06x\n", status,
                lastErrorCode);
            free(lastErrorMessage);
            return PEAK_FALSE;
        }

        printf("Last-Error: %s | Code: %#06x\n", lastErrorMessage, lastErrorCode);
        free(lastErrorMessage);

        return PEAK_FALSE;
    }
    return PEAK_TRUE;
}
#endif


#ifdef IDS_PEAK
/**
 * @brief Get the current trigger delay in microseconds
 * 
 * @param[out] pCurrentTriggerDelayUs 
 * @return int -1 if failed, 0 otherwise
 */
int getTriggerDelay(double* pCurrentTriggerDelayUs)
{
    int ret = 0;
    peak_access_status accessStatus = peak_Trigger_Delay_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_status status = peak_Trigger_Delay_Get(hCam, pCurrentTriggerDelayUs);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "getTriggerDelay: Checking trigger delay value failed.\n");
            ret = -1;
        } else {
            if (verbose) {
                printf("getTriggerDelay: actual trigger delay is %f\n", *pCurrentTriggerDelayUs);
            }
        }
    } else {
        fprintf(stderr, "getTriggerDelay: Failed to get actual trigger delay "
                "value, trigger delay not readable at this time.\n");
        ret = -1;
    }
    return ret;
}

/**
 * @brief Set the Trigger Delay to the minimum possible value. Side effect:
 * updates metadata struct
 * 
 * @return int -1 if failed, 0 otherwise
 */
int setMinTriggerDelay(void)
{
    int ret = 0;
    // Zero out trigger delay
    peak_access_status accessStatus = peak_Trigger_Delay_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        // if writeable, call the setter function
        // Be sure that the new value is within the valid range
        double doubleMin = 0.0;
        double doubleMax = 0.0;
        double doubleInc = 0.0;
        peak_status status = peak_Trigger_Delay_GetRange(hCam, &doubleMin, &doubleMax, &doubleInc);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "setTriggerDelay: Checking trigger delay range "
                "failed. Attempting to set to 0 in absence of min val.\n");
            ret = -1;
        }

        status = peak_Trigger_Delay_Set(hCam, doubleMin);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "setTriggerDelay: Setting trigger delay to min "
                "failed.\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "setTriggerDelay: Failed to set state of trigger delay,"
                        " not writeable at this time.\n");
        ret = -1;
    }

    // Update metadata
    double actualTriggerDelayUs = -1.0;
    getTriggerDelay(&actualTriggerDelayUs);
    default_metadata.trigdlay = (float)(actualTriggerDelayUs / 1000.0);

    return ret;
}

/**
 * @brief Get the current Trigger Divider value
 * @details A trigger divider value of e.g. 3 will cause every 3rd trigger to be
 * listened to.
 * 
 * @param pCurrentTriggerDivider 
 * @return int -1 if failed, 0 otherwise
 */
int getTriggerDivider(uint32_t* pCurrentTriggerDivider)
{
    int ret = 0;
    peak_access_status accessStatus = peak_Trigger_Divider_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        uint32_t actualTriggerDivider = 1;
        peak_status status = peak_Trigger_Divider_Get(hCam, pCurrentTriggerDivider);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "getTriggerDivider: Checking trigger divider value "
                 "failed.\n");
                ret = -1;
        } else {
            if (verbose) {
                printf("getTriggerDivider: actual trigger divider is %d\n",
                    *pCurrentTriggerDivider);
            }
        }
    } else {
        fprintf(stderr, "getTriggerDivider: Failed to get actual trigger "
            "divider value, trigger divider not readable at this time.\n");
        ret = -1;
    }
    return ret;
}

/**
 * @brief Set the Trigger Divider to 1
* @details A trigger divider value of e.g. 3 will cause every 3rd trigger to be
 * listened to.
 * @return int 
 */
int setMinTriggerDivider(void) {
    int ret = 0;
    // Assume we always want to use each trigger, and that we can always set the
    // divider to 1.
    peak_access_status accessStatus = peak_Trigger_Divider_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        // Eschew range checks. 1 will always be a valid divisor.
        peak_status status = peak_Trigger_Divider_Set(hCam, 1);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "setTriggerDivider: Setting trigger divider failed."
                "\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "setTriggerDivider: Failed to set state of trigger "
            "divider, not writeable at this time.\n");
        ret =-1;
    }

    // Report if verbose
    uint32_t dummy = 1;
    getTriggerDivider(&dummy);

    return ret;
}


/**
 * @brief Get the analog gain factor.
 * 
 * @param[out] pAnalogGain pointer to output actual analog gain factor
 * @return int -1 if failed, 0 otherwise
 */
int getMonoAnalogGain(double* pAnalogGain)
{
    int ret = 0;
    peak_access_status accessStatus = peak_Gain_GetAccessStatus(hCam,
        PEAK_GAIN_TYPE_ANALOG, PEAK_GAIN_CHANNEL_MASTER);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_status status = peak_Gain_Get(hCam, PEAK_GAIN_TYPE_ANALOG,
            PEAK_GAIN_CHANNEL_MASTER, pAnalogGain);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "Failed to check actual gain.\n");
            ret = -1;
        } else {
            if (verbose) {
                printf("getMonoAnalogGain: actual gain is %fx\n", *pAnalogGain);
            }
        }
    } else {
        fprintf(stderr, "getMonoAnalogGain: Failed to get actual gain, gain not"
            " readable at this time.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief Set the Mono Analog Gain factor. This is the integrated amplifier gain
 * on-chip, it is not a multiplicative digital gain applied after quantization. 
 * Side effect: updates metadata struct
 * 
 * @param analogGain multiplicative factor applied to the base gain
 * @return int -1 if failed, 0 otherwise
 */
int setMonoAnalogGain(double analogGain)
{
    int ret = 0;
    peak_status status = PEAK_STATUS_OUT_OF_RANGE;

    // Only attempt if gain limits are readable
    peak_access_status accessStatus = peak_Gain_GetAccessStatus(hCam,
        PEAK_GAIN_TYPE_ANALOG, PEAK_GAIN_CHANNEL_MASTER);
    if (PEAK_IS_READABLE(accessStatus)) {
        // Check the gain against the limits
        double minGain = 0.0;
        double maxGain = 0.0;
        double incGain = 0.0;
        status = peak_Gain_GetRange(hCam, PEAK_GAIN_TYPE_ANALOG,
            PEAK_GAIN_CHANNEL_MASTER, &minGain, &maxGain, &incGain);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "setMonoAnalogGain: Failed to check gain range. Not"
                " setting gain.\n");
            ret = -1;
        } else {
            // Set gain, limited by range
            if (verbose) {
                printf("setMonoAnalogGain: Available gain range is %f - %f, "
                    "step %f.\n", minGain, maxGain, incGain);
            }
            analogGain = (minGain > analogGain) ? minGain : analogGain;
            analogGain = (maxGain < analogGain) ? maxGain : analogGain;
            if (PEAK_IS_WRITEABLE(accessStatus)) {
                status = peak_Gain_Set(hCam, PEAK_GAIN_TYPE_ANALOG,
                    PEAK_GAIN_CHANNEL_MASTER, analogGain);
                if (!checkForSuccess(status)) {
                    fprintf(stderr, "setMonoAnalogGain: Failed to set requested"
                        " gain value %f.\n", analogGain);
                    ret = -1;
                }
            } else {
                fprintf(stderr, "setMonoAnalogGain: Failed to set requested "
                    "gain value %f. Gain is not writeable at this time.\n",
                    analogGain);
                ret = -1;
            }
        }
    } else {
        fprintf(stderr, "Failed to set requested gain value %f. Gain limits are"
             " not readable at this time.\n", analogGain);
        ret = -1;
    }

    // Update metadata
    double actualAnalogGain = 1.0;
    getMonoAnalogGain(&actualAnalogGain);
    default_metadata.gainfact = (float)actualAnalogGain;

    return ret;
}


/**
 * @brief Get the Auto Exposure Enabled state
 * 
 * @param pIsEnabled 
 * @return int -1 if failed, 0 otherwise
 */
int getAutoExposureEnabled(int *pIsEnabled)
{
    int ret = 0;
    peak_access_status accessStatus = peak_AutoBrightness_Exposure_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_auto_feature_mode mode = PEAK_AUTO_FEATURE_MODE_INVALID;
        peak_status status = peak_AutoBrightness_Exposure_Mode_Get(hCam, &mode);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "getAutoExposureEnabled: Checking autoexposure "
                    "mode failed.\n");
                    ret = -1;
        } else {
            if (PEAK_AUTO_FEATURE_MODE_OFF == mode) {
                *pIsEnabled = 0;
            }
            if (verbose) {
                printf("getAutoExposureEnabled: %d\n", *pIsEnabled);
            }
        }
    } else {
        fprintf(stderr, "getAutoExposureEnabled: Failed to get autoexposure "
            "state, not readable at this time.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief Set auto exposure to off. Side effect: updates metadata struct
 * 
 * @return int -1 if failed, 0 otherwise
 */
int disableAutoExposure(void)
{
    int ret = 0;
    peak_access_status accessStatus = peak_AutoBrightness_Exposure_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        peak_status status = peak_AutoBrightness_Exposure_Mode_Set(hCam,
            PEAK_AUTO_FEATURE_MODE_OFF);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "disableAutoExposure: Setting autoexposure off "
                "failed.\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "disableAutoExposure: Failed to set autoexposure, not "
            "writeable at this time.\n");
        ret = -1;
    }

    // Update metadata
    int autoExposureIsEnabled = 0;
    getAutoExposureEnabled(&autoExposureIsEnabled);
    default_metadata.autoexp = (int8_t)autoExposureIsEnabled;

    return ret;
}


/**
 * @brief Get the Auto Gain Enabled state
 * 
 * @param pIsEnabled
 * @return int -1 if failed, 0 otherwise
 */
int getAutoGainEnabled(int* pIsEnabled)
{
    int ret = 0;
    peak_access_status accessStatus = peak_AutoBrightness_Gain_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_auto_feature_mode mode = PEAK_AUTO_FEATURE_MODE_INVALID;
        peak_status status = peak_AutoBrightness_Gain_Mode_Get(hCam, &mode);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "getAutoGainEnabled: Checking autogain mode "
                "failed.\n");
            ret = -1;
        } else {
            if (PEAK_AUTO_FEATURE_MODE_OFF == mode) {
                *pIsEnabled = 0;
            }
            if (verbose) {
                printf("getAutoGainEnabled: %d\n", *pIsEnabled);
            }
        }
    } else {
        fprintf(stderr, "getAutoGainEnabled: Failed to get autogain state, "
            "not readable at this time.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief disables auto gain control. Side effect: updates metadata struct
 * 
 * @return int -1 if failed, 0 otherwise
 */
int disableAutoGain(void)
{
    int ret = 0;
    peak_access_status accessStatus = peak_AutoBrightness_Gain_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        peak_status status = peak_AutoBrightness_Gain_Mode_Set(hCam,
            PEAK_AUTO_FEATURE_MODE_OFF);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "disableAutoGain: Setting autogain off failed. "
                "Continuing.\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "disableAutoGain: Failed to set autogain, not "
            "writeable at this time.\n");
        ret = -1;
    }

    // Update metadata
    int autoGainIsEnabled = 0;
    getAutoGainEnabled(&autoGainIsEnabled);
    default_metadata.autogain = (int8_t)autoGainIsEnabled;

    return ret;
}
#endif


#ifndef IDS_PEAK
double getFps(void)
{
    double actual_fps = 0;
    is_SetFrameRate(camera_handle, IS_GET_FRAMERATE, (void *) &actual_fps);
    return actual_fps;
}
#else
/**
 * @brief Get the current framerate in Hz
 * 
 * @param pCurrentFps 
 * @return int 
 */
int getFps(double* pCurrentFps)
{
    int ret = 0;
    peak_access_status accessStatus = peak_FrameRate_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_status status = peak_FrameRate_Get(hCam, pCurrentFps);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "getFps: Getting framerate failed.\n");
            ret = -1;
        } else {
            if (verbose) {
                printf("getFps: actual framerate value is %f Hz\n",
                    *pCurrentFps);
            }
        }
    } else {
        fprintf(stderr, "getFps: Failed to get actual framerate value, "
            "framerate not readable at this time.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief Set the framerate, although it doesn't apply in software trigger mode,
 * only in freerun mode. Side effect: updates metadata struct
 * 
 * @param frameRateFps
 * @return int -1 if failed, 0 otherwise
 */
int setFps(double frameRateFps)
{
    int ret = 0;
    peak_access_status accessStatus = peak_FrameRate_GetAccessStatus(hCam);
    // Attempt to write
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        peak_status status = peak_FrameRate_Set(hCam, frameRateFps);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "Setting framerate failed. Continuing.\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "setFps: Failed to set framerate value, framerate not "
            "writeable at this time.\n");
        ret = -1;
    }

    // Update metadata
    double currentFps = 10.0;;
    getFps(&currentFps);
    default_metadata.framerte = (float)currentFps;

    return ret;
}


/**
 * @brief Get the current Pixel Clock
 * 
 * @param[out] pCurrentPixelClockMHz 
 * @return int -1 if failed, 0 otherwise
 */
int getPixelClock(double* pCurrentPixelClockMHz)
{
    int ret = 0;
    peak_access_status accessStatus = peak_PixelClock_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_status status = peak_PixelClock_Get(hCam, pCurrentPixelClockMHz);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "getPixelClock: Getting pixel clock failed.\n");
            ret = -1;
        } else {
            if (verbose) {
                printf("getPixelClock: actual pixelclock value is %f MHz\n",
                    *pCurrentPixelClockMHz);
            }
        }
    } else {
        fprintf(stderr, "getPixelClock: Failed to get actual pixelclock value,"
            " pixel clock not readable at this time.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief Sets the Pixel Clock to the minimum available value.
 * @details For the U3-31N0CP-M-GL, the only option is 216 MHz.
 * 
 * @return int -1 if failed, 0 otherwise
 */
int setMinPixelClock(void)
{
    int ret = 0;
    // Default to setting the pixelclock to the lowest allowed value in the
    // range or list.
    // The organization of the valid values for the pixel clock feature depends
    // on the specific camera.
    // So, first we have to decide whether to query a range or a list.
    double pixelClockValueMHz = 261.0; // Min value for U3-N0CP, 12bit mono
    peak_access_status accessStatus = peak_PixelClock_GetAccessStatus(hCam);
    peak_status status = PEAK_STATUS_SUCCESS;
    if (PEAK_IS_READABLE(accessStatus)) {
        if (peak_PixelClock_HasRange(hCam) == PEAK_TRUE) {
            // The valid values are organized as a range.
            double minPixelClockMHz = 0.0;
            double maxPixelClockMHz = 0.0;
            double incPixelClockMHz = 0.0;
            status = peak_PixelClock_GetRange(hCam, &minPixelClockMHz,
                &maxPixelClockMHz, &incPixelClockMHz);
            if (checkForSuccess(status)) {
                // A valid value for the pixel clock is between minPixelClockMHz
                // and maxPixelClockMHz with an increment of incPixelClockMHz.
                pixelClockValueMHz = minPixelClockMHz;
            }
            if (verbose) {
                printf("setMinPixelClock: got range %f - %f, step %f MHz.\n",
                    minPixelClockMHz, maxPixelClockMHz, incPixelClockMHz);
            }
        } else {
            // The valid values are organized as a list.
            size_t pixelClockCount = 0;
            status = peak_PixelClock_GetList(hCam, NULL,
                &pixelClockCount);
            if (checkForSuccess(status)) {
                double* pixelClockList = (double*)malloc(pixelClockCount * sizeof(double));
                memset(pixelClockList, 0, pixelClockCount * sizeof(double));
                status = peak_PixelClock_GetList(hCam, pixelClockList,
                    &pixelClockCount);
                if (checkForSuccess(status)) {
                    // A valid value for the pixel clock is one of pixelClockList.
                    pixelClockValueMHz = pixelClockList[0];
                }
                if (verbose) {
                    printf("setMinPixelClock: got list:\n");
                    for (int i = 0; i < pixelClockCount; i++) {
                        printf("\t- %f\n", pixelClockList[i]);
                    }
                }
            }
        }
    }  else {
        fprintf(stderr, "WARNING: setMinPixelClock: Failed to get pixelclock "
            "minimum value, pixel clock not readable at this time.\n");
    }

    accessStatus = peak_PixelClock_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        status = peak_PixelClock_Set(hCam, pixelClockValueMHz);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "setMinPixelClock: Setting pixel clock failed.\n");
            ret = -1;
        } else {
            // Things are fine, actually, if we happen to set it correctly
            // despite not knowing the limits
            ret = 0;
        }
    } else {
        // Some cameras may not have user-settable pixel clocks. Don't fail if 
        // setting fails due to readonly.
        if (PEAK_ACCESS_READONLY == accessStatus) {
            ret = 0;
        } else {
            fprintf(stderr, "setMinPixelClock: Failed to set pixelclock "
                "to minimum, pixel clock not writeable at this time.\n");
            ret = -1;
        }
    }

    // Update metadata
    double actualPixelClockMHz = 0.0;
    getPixelClock(&actualPixelClockMHz);
    default_metadata.pixelclk = (float)actualPixelClockMHz;

    return ret;
}
#endif


#ifndef IDS_PEAK
/**
 * @brief Attempt to set exposure time to the user-commanded time in the
 * all_camera_params.exposure_time struct field.
 * @details Pass in the struct field by value to avoid is_Exposure updating it
 * out from under the user. We do report the actual set value though.
 * 
 * @param exposureTimeMs requested exposure time (ms)
 * @return int -1 for failure, 0 otherwise
 */
int setExposureTime(double exposureTimeMs)
{
    int ret = 0;
    // run uEye function to update camera exposure
    // After the call, the actual set exposure time is stored in exposureTimeMs
    if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_SET_EXPOSURE, 
                    (void *) &exposureTimeMs, 
                    sizeof(double)) != IS_SUCCESS) {
        printf("Adjusting exposure to user command unsuccessful.\n");
        ret = -1;
    }
    if (verbose) {
        printf("Exposure is now %f msec.\n", exposureTimeMs);
    }
    return ret;
}
#else
/**
 * @brief Get the exposure time in milliseconds.
 * 
 * @param[out] pExposureTimeMs pointer to output actual exposure time (ms)
 * @return int -1 if failed, 0 otherwise
 */
int getExposureTime(double* pExposureTimeMs)
{
    int ret = 0;
    double actualExposureTimeUs = 0.0;

    peak_access_status accessStatus = peak_ExposureTime_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_status status = peak_ExposureTime_Get(hCam, &actualExposureTimeUs);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "getExposureTime: Failed to get actual exposure "
                "time.\n");
            ret = -1;
        } else {
            if (verbose) {
                printf("getExposureTime: Actual exposure time is %f us\n",
                    actualExposureTimeUs);
            }
            *pExposureTimeMs = actualExposureTimeUs / 1000.0;
        }
    } else {
        fprintf(stderr, "getExposureTime: Failed to get actual exposure "
            "time, exposure time not readable at this time.\n");
        ret = -1;
    }

    return ret;
}


/**
 * @brief Set the exposure time in milliseconds. If the request value is not
 * available, the closest available value is chosen. Side effect: updates
 * metadata struct.
 * 
 * @param exposureTimeMs requested exposure time (ms)
 * @return int -1 if failed, 0 otherwise
 */
int setExposureTime(double exposureTimeMs)
{
    int ret = 0;
    // User gives ms, API wants us
    double exposureTimeUs = 1000.0 * exposureTimeMs;

    peak_status status = PEAK_STATUS_SUCCESS;
    peak_access_status accessStatus = peak_ExposureTime_GetAccessStatus(hCam);
    // Check request against available range
    if (PEAK_IS_READABLE(accessStatus)) {
        double minExposureTimeUs = 0.0;
        double maxExposureTimeUs = 0.0;
        double incExposureTimeUs = 0.0;
        status = peak_ExposureTime_GetRange(hCam, &minExposureTimeUs,
            &maxExposureTimeUs, &incExposureTimeUs);

        if (!checkForSuccess(status)) {
            fprintf(stderr, "setExposureTime: Failed to check exposure time "
                "range. Continuing.\n");
        } else {
            if (verbose) {
                printf("setExposureTime: Available exposure time range is "
                    "%f - %f, step %f us.\n", minExposureTimeUs,
                    maxExposureTimeUs, incExposureTimeUs);
            }
        }

        if (exposureTimeUs < minExposureTimeUs) {
            exposureTimeUs = minExposureTimeUs;
            printf("setExposureTime: Requested exposure too short. Corrected to %f us.\n",
                exposureTimeUs);
        } else if (exposureTimeUs > maxExposureTimeUs) {
            exposureTimeUs = maxExposureTimeUs;
            printf("setExposureTime: Requested exposure too long. Corrected to %f us.\n",
                exposureTimeUs);
        }
    }
    // A failure to check exposure time range is not an immediate fail; we may
    // still attempt to set it.

    // Only attempt if exposure time is writable
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        status = peak_ExposureTime_Set(hCam, exposureTimeUs);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "setExposureTime: Failed to set requested exposure "
                "time %f us.\n", exposureTimeUs);
            ret = -1;
        }
    } else {
        fprintf(stderr, "setExposureTime: Exposure time is not writeable at "
            "this time.\n");
        ret = -1;
    }

    // Update metadata
    double actualExpTimeMs = -1.0;
    getExposureTime(&actualExpTimeMs);
    // FITS files want seconds, not ms
    default_metadata.exptime = (float)(actualExpTimeMs / 1000.0);

    return ret;
}


/**
 * @brief Get the Auto Black Level Enabled state
 * 
 * @param[out] pIsEnabled
 * @return int
 */
int getAutoBlackLevelEnabled(int* pIsEnabled)
{
    int ret = 0;
    peak_auto_feature_mode modeStatus = PEAK_AUTO_FEATURE_MODE_INVALID;
    peak_access_status accessStatus = peak_BlackLevel_Auto_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        if(PEAK_TRUE == peak_BlackLevel_Auto_IsEnabled(hCam)) {
            *pIsEnabled = 1;
        } else {
            *pIsEnabled = 0;
        }
    } else {
        fprintf(stderr, "getAutoBlackLevelEnabled: Failed to get state of auto "
            "black level offset, not readable at this time.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief disable automatic black level control. Side effect: updates metadata
 * struct
 * 
 * @return int -1 if failed, 0 otherwise
 */
int disableAutoBlackLevel(void)
{
    int ret = 0;
    peak_access_status accessStatus = peak_BlackLevel_Auto_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        peak_status status = peak_BlackLevel_Auto_Enable(hCam, PEAK_FALSE);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "disableAutoBlackLevel: Setting auto black level "
                "off failed.\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "disableAutoBlackLevel: Failed to disable auto black "
            "level offset, not writeable at this time.\n");
        ret = -1;
    }

    // Update metadata
    int autoBlackLevelIsEnabled = 0;
    getAutoBlackLevelEnabled(&autoBlackLevelIsEnabled);
    default_metadata.autoblk = (int8_t)autoBlackLevelIsEnabled;

    return ret;
}


/**
 * @brief Get the current Black Level Offset
 * 
 * @param[out] pCurrentBlackLevelOffset 
 * @return int 
 */
int getBlackLevelOffset(double* pCurrentBlackLevelOffset)
{
    int ret = 0;
    peak_access_status accessStatus = peak_BlackLevel_Offset_GetAccessStatus(hCam);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_status status = peak_BlackLevel_Offset_Get(hCam, pCurrentBlackLevelOffset);
        if(!checkForSuccess(status)) {
            fprintf(stderr, "getBlackLevelOffset: Failed to get black level "
                "offset.\n");
            ret = -1;
        } else {
            if (verbose) {
                printf("getBlackLevelOffset: actual black level offset is %f\n",
                     *pCurrentBlackLevelOffset);
            }
        }
    } else {
        fprintf(stderr, "getBlackLevelOffset: Failed to get actual black level "
            "offset value, value not readable at this time.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief Set the Black Level Offset. The black level is added uniformly to all
 * pixels being read out, to avoid histogram/ADC clipping at the 0 end. Side
 * effect: updates metadata struct
 * @details The black level should be set after changing the pixel format (bit
 * depth)
 * @param blackLevelOffset 
 * @return int -1 if failed, 0 otherwise
 */
int setBlackLevelOffset(double blackLevelOffset)
{
    int ret = 0;
    peak_access_status accessStatus = peak_BlackLevel_Offset_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        peak_status status = peak_BlackLevel_Offset_Set(hCam, blackLevelOffset);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "setBlackLevelOffset: Setting black level offset "
                "value failed.\n");
            ret = -1;
        }
    } else {
        fprintf(stderr, "setBlackLevelOffset: Failed to set black level offset "
            "value, value not writeable at this time.\n");
        ret = -1;
    }

    // Update metadata
    double actualBlackLevelOffset = 0.0;
    getBlackLevelOffset(&actualBlackLevelOffset);
    default_metadata.bloffset = (float)actualBlackLevelOffset;

    return ret;
}


#endif

#ifndef IDS_PEAK
/* Function to initialize the camera and its various parameters.
** Input: None.
** Output: A flag indicating successful camera initialization or not.
*/
int initCamera(void)
{
    double min_exposure, max_exposure;
    unsigned int enable = 1;

    // load the camera parameters
    if (loadCamera() < 0) {
        return -1;
    }

    // enable long exposure 
    if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_SET_LONG_EXPOSURE_ENABLE, 
                   (void *) &enable, sizeof(unsigned int)) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Unable to enable long exposure: %s.\n", cam_error);
        return -1; 
    }

    // set exposure time based on struct field
    if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_SET_EXPOSURE, 
                   (void *) &all_camera_params.exposure_time, sizeof(double)) 
                   != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Unable to set default exposure: %s.\n", cam_error);
        return -1;
    }

    // get current exposure, max possible exposure, and min exposure
    if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_MIN, 
                    &min_exposure, sizeof(double)) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Unable to get minimum possible exposure: %s.\n", cam_error);
        return -1;
    }

    if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_GET_EXPOSURE, &curr_exposure,
                    sizeof(double)) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Unable to get current exposure value: %s.\n", cam_error);
        return -1;
    }

    if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_MAX, 
                    &max_exposure, sizeof(double)) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Unable to get maximum exposure value: %s.\n", cam_error);
        return -1;
    } else if (verbose) {
        printf("|\tCurrent exposure time: %f msec\t\t  |\n|\tMin possible "
               "exposure: %f msec\t\t  |\n|\tMax possible exposure: %f "
               "msec\t  |\n", curr_exposure, min_exposure, max_exposure);
        printf("+---------------------------------------------------------+\n");
    }

    // initialize astrometry
    if (initAstrometry() < 0) {
        return -1;
    }

    // set how images are saved
    setSaveImage();

    return 1;
}
#else
int initCamera(void)
{
    // load the camera parameters
    if (loadCamera() < 0) {
        return -1;
    }

    // // enable long exposure 
    // if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_SET_LONG_EXPOSURE_ENABLE, 
    //                (void *) &enable, sizeof(unsigned int)) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Unable to enable long exposure: %s.\n", cam_error);
    //     return -1; 
    // }

    // Long exposure mode does not exist in iDS peak

    // // set exposure time based on struct field
    // if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_SET_EXPOSURE, 
    //                (void *) &all_camera_params.exposure_time, sizeof(double)) 
    //                != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Unable to set default exposure: %s.\n", cam_error);
    //     return -1;
    // }
    // // get current exposure, max possible exposure, and min exposure
    // if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_MIN, 
    //                 &min_exposure, sizeof(double)) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Unable to get minimum possible exposure: %s.\n", cam_error);
    //     return -1;
    // }
    // if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_GET_EXPOSURE, &curr_exposure,
    //                 sizeof(double)) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Unable to get current exposure value: %s.\n", cam_error);
    //     return -1;
    // }
    // if (is_Exposure(camera_handle, IS_EXPOSURE_CMD_GET_EXPOSURE_RANGE_MAX, 
    //                 &max_exposure, sizeof(double)) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Unable to get maximum exposure value: %s.\n", cam_error);
    //     return -1;
    // } else if (verbose) {
    //     printf("|\tCurrent exposure time: %f msec\t\t  |\n|\tMin possible "
    //            "exposure: %f msec\t\t  |\n|\tMax possible exposure: %f "
    //            "msec\t  |\n", curr_exposure, min_exposure, max_exposure);
    //     printf("+---------------------------------------------------------+\n");
    // }

    // Set exposure mode based on user input
    // User requests milliseconds
    if (setExposureTime(all_camera_params.exposure_time) < 0) {
        return -1;
    }

    // initialize astrometry
    if (initAstrometry() < 0) {
        return -1;
    }

    // // set how images are saved
    // setSaveImage();

    // In place of logging all sensor parameters to a text file, save off the
    // camera parameter file.
    // This file is human-readable, and can be used to replicate the device
    // settings in iDS peak cockpit or by loading in another application.
    char initialParameterFile[] = "/home/starcam/Desktop/TIMSC/parameters_last_startup.cset";
    if (verbose) {
        printf("initCamera: dumping initial parameters to %s...\n", initialParameterFile);
    }
    peak_status status = peak_CameraSettings_DiskFile_Store(hCam,
        initialParameterFile);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to dump camera settings to disk %s on "
            "startup.\n", initialParameterFile);
    }

    if (verbose) {
        printf("initCamera: starting image acquisition...\n");
    }
    // Only after all parameters that impact the image size are set, start
    // infinite image acquisition, will be triggered by software.
    // Buffer is automatically allocated in the following call:
    status = peak_Acquisition_Start(hCam, PEAK_INFINITE);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to start image acquisition. Exiting.\n");
        return -1;
    }

    return 1;
}
#endif


/* Cleaning up function for ctrl+c exception.
** Input: None.
** Output: None (void). Changes the state of the camera to shutting down.
**/
void clean(void)
{
    if (verbose) {
        printf("\n> Entering shutting down state...\n");
    }
    // camera is in shutting down state (stop solving)
    shutting_down = 1;
}


#ifndef IDS_PEAK
/* Function to close the camera when shutting down.
** Input: None.
** Output: None (void).
*/
void closeCamera(void)
{
    if (verbose) {
        printf("> Closing camera with handle %d...\n", camera_handle);
    }

    // don't close a camera that doesn't exist yet!
    if ((mem_starting_ptr != NULL) && (camera_handle <= 254)) { 
        is_FreeImageMem(camera_handle, (char *)mem_starting_ptr, mem_id);
        is_ExitCamera(camera_handle);
    }
}
#else
void closeCamera(void)
{
    // if (verbose) {
    //     printf("> Closing camera with handle %d...\n", camera_handle);
    // }
    
    // // don't close a camera that doesn't exist yet!
    // if ((mem_starting_ptr != NULL) && (camera_handle <= 254)) { 
    //     is_FreeImageMem(camera_handle, (char *)mem_starting_ptr, mem_id);
    //     is_ExitCamera(camera_handle);
    // }

    // Stop acquisition if ongoing
    if (PEAK_TRUE == peak_Acquisition_IsStarted(hCam)) {
        peak_status status = peak_Acquisition_Stop(hCam);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Failed to stop image acquisition. Exiting "
                "anyway.\n");
        }
    }

    // Save off the parameters at shutdown
    char finalParameterFile[] = "/home/starcam/Desktop/TIMSC/parameters_last_shutdown.cset";
    if (verbose) {
        printf("closeCamera: dumping final parameters to %s...\n", finalParameterFile);
    }
    peak_status status = peak_CameraSettings_DiskFile_Store(hCam,
        finalParameterFile);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to dump camera settings to disk %s on "
            "shutdown.\n", finalParameterFile);
    }


    if (verbose) {
        printf("Closing camera...\n");
    }
    (void)peak_Camera_Close(hCam);
    hCam = NULL;
    (void)peak_Library_Exit();
}
#endif


#ifndef IDS_PEAK
/* Function to set starting values for certain camera atributes.
** Input: None.
** Output: A flag indicating successful setting of the camera parameters.
*/
int setCameraParams(void)
{
    double ag = 0.0, auto_shutter = 0.0, afr = 0.0;
    int blo, blm;

    // set the trigger delay to 0 (microseconds) = deactivates trigger delay
    if (is_SetTriggerDelay(camera_handle, 0) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting the trigger to 0 microseconds: %s.\n", cam_error);
        return -1;
    }
    // check the trigger delay is 0
    curr_trig_delay = is_SetTriggerDelay(camera_handle, IS_GET_TRIGGER_DELAY); 
    if (verbose) {
        printf("\n+---------------------------------------------------------+\n");
        printf("|\t\tCamera Specifications\t\t\t  |\n");
        printf("|---------------------------------------------------------|\n");
        printf("|\tTrigger delay (should be 0): %i\t\t\t  |\n", curr_trig_delay);
    }

    // set camera integrated amplifier 
    if (is_SetHardwareGain(camera_handle, 0, IS_IGNORE_PARAMETER, 
                           IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER) 
                           != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting gain: %s.\n", cam_error);
        return -1;
    }
    // check color gains
    curr_master_gain = is_SetHardwareGain(camera_handle, IS_GET_MASTER_GAIN, 
                                          IS_IGNORE_PARAMETER, 
                                          IS_IGNORE_PARAMETER, 
                                          IS_IGNORE_PARAMETER);
    curr_red_gain = is_SetHardwareGain(camera_handle, IS_IGNORE_PARAMETER, 
                                       IS_GET_RED_GAIN, IS_IGNORE_PARAMETER, 
                                       IS_IGNORE_PARAMETER);
    curr_green_gain = is_SetHardwareGain(camera_handle, IS_IGNORE_PARAMETER, 
                                         IS_IGNORE_PARAMETER, IS_GET_GREEN_GAIN,
                                         IS_IGNORE_PARAMETER);
    curr_blue_gain = is_SetHardwareGain(camera_handle, IS_IGNORE_PARAMETER, 
                                        IS_IGNORE_PARAMETER, 
                                        IS_IGNORE_PARAMETER, IS_GET_BLUE_GAIN);
    if (verbose) {
        printf("|\tGain: master, red, green, blue = %i, %i, %i, %i |\n", 
               curr_master_gain, curr_red_gain, curr_green_gain, curr_blue_gain);
    }

    // disable camera's auto gain (0 = disabled)
    if (is_SetAutoParameter(camera_handle, IS_SET_ENABLE_AUTO_GAIN, &ag, NULL) 
        != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error disabling auto gain: %s.\n", cam_error);
        return -1;
    }
    // check auto gain is off
    is_SetAutoParameter(camera_handle, IS_GET_ENABLE_AUTO_GAIN, &curr_ag, NULL);
    if (verbose) {
        printf("|\tAuto gain (should be disabled): %.1f\t\t  |\n", curr_ag);
    }

    // set camera's gamma value to off 
    if (is_SetHardwareGamma(camera_handle, IS_SET_HW_GAMMA_OFF) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error disabling hardware gamma correction: %s.\n", cam_error);
        return -1;
    }
    // check hardware gamma is off
    curr_gamma = is_SetHardwareGamma(camera_handle, IS_GET_HW_GAMMA);
    if (verbose) {
        printf("|\tHardware gamma (should be off): %i\t\t  |\n", curr_gamma);
    }

    // disable auto shutter
    if (is_SetAutoParameter(camera_handle, IS_SET_ENABLE_AUTO_SHUTTER, 
                            &auto_shutter, NULL) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error disabling auto shutter: %s.\n", cam_error);
        return -1;
    }
    // check auto shutter is off
    is_SetAutoParameter(camera_handle, IS_GET_ENABLE_AUTO_SHUTTER, 
                        &curr_shutter, NULL);
    if (verbose) {
        printf("|\tAuto shutter (should be off): %.1f\t\t  |\n", curr_shutter);
    }

    // disable auto frame rate
    if (is_SetAutoParameter(camera_handle, IS_SET_ENABLE_AUTO_FRAMERATE, &afr, 
                            NULL) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error disabling auto frame rate: %s.\n", cam_error);
        return -1;
    }
    // check auto frame rate is off
    is_SetAutoParameter(camera_handle, IS_GET_ENABLE_AUTO_FRAMERATE, &auto_fr, 
                        NULL);
    if (verbose) {
        printf("|\tAuto frame rate (should be off): %.1f\t\t  |\n", auto_fr);
    }

    // disable gain boost to reduce noise
    if (is_SetGainBoost(camera_handle, IS_SET_GAINBOOST_OFF) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error disabling gain boost: %s.\n", cam_error);
        return -1;
    }
    // check gain boost is off
    curr_gain_boost = is_SetGainBoost(camera_handle, IS_GET_GAINBOOST);
    if (verbose) {
        printf("|\tGain boost (should be disabled): %i\t\t  |\n", 
               curr_gain_boost);	
    }

    // turn off auto black level
    blm = IS_AUTO_BLACKLEVEL_OFF;
    if (is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_SET_MODE, (void *) &blm, 
                      sizeof(blm)) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error turning off auto black level mode: %s.\n", cam_error);
        return -1;
    }
    // check auto black level is off
    bl_mode = is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_GET_MODE, 
                            (void *) &bl_mode, sizeof(bl_mode));
    if (verbose) {
        printf("|\tAuto black level (should be off): %i\t\t  |\n", bl_mode);
    }

    // set black level offset to 50
    blo = 50;
    if (is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_SET_OFFSET, 
                      (void *) &blo, sizeof(blo)) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting black level offset to 50: %s.\n", cam_error);
        return -1;
    }
    // check black level offset
    is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_GET_OFFSET, 
                  (void *) &bl_offset, sizeof(bl_offset));
    if (verbose) {
        printf("|\tBlack level offset (desired is 50): %i\t\t  |\n", bl_offset);
    }

    // This for some reason actually affects the time it takes to set aois only 
    // on the focal plane camera. Don't use if for the trigger timeout. 
    if (is_SetTimeout(camera_handle, IS_TRIGGER_TIMEOUT, 500) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting trigger timeout: %s.\n", cam_error);
        return -1;
    }
    // check time out
    is_GetTimeout(camera_handle, IS_TRIGGER_TIMEOUT, &curr_timeout);
    if (verbose) {
        printf("|\tCurrent trigger timeout: %i\t\t\t  |\n", curr_timeout);
    }

    return 1;
}
#else
int setCameraParams(void)
{
    // // set the trigger delay to 0 (microseconds) = deactivates trigger delay
    // if (is_SetTriggerDelay(camera_handle, 0) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting the trigger to 0 microseconds: %s.\n", cam_error);
    //     return -1;
    // }
    // // check the trigger delay is 0
    // curr_trig_delay = is_SetTriggerDelay(camera_handle, IS_GET_TRIGGER_DELAY); 
    // if (verbose) {
    //     printf("\n+---------------------------------------------------------+\n");
    //     printf("|\t\tCamera Specifications\t\t\t  |\n");
    //     printf("|---------------------------------------------------------|\n");
    //     printf("|\tTrigger delay (should be 0): %i\t\t\t  |\n", curr_trig_delay);
    // }

    if (setMinTriggerDelay() < 0) {
        return -1;
    }

    if (setMinTriggerDivider() < 0) {
        return -1;
    }

    // // set camera integrated amplifier 
    // if (is_SetHardwareGain(camera_handle, 0, IS_IGNORE_PARAMETER, 
    //                        IS_IGNORE_PARAMETER, IS_IGNORE_PARAMETER) 
    //                        != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting gain: %s.\n", cam_error);
    //     return -1;
    // }
    // // check color gains
    // curr_master_gain = is_SetHardwareGain(camera_handle, IS_GET_MASTER_GAIN, 
    //                                       IS_IGNORE_PARAMETER, 
    //                                       IS_IGNORE_PARAMETER, 
    //                                       IS_IGNORE_PARAMETER);
    // curr_red_gain = is_SetHardwareGain(camera_handle, IS_IGNORE_PARAMETER, 
    //                                    IS_GET_RED_GAIN, IS_IGNORE_PARAMETER, 
    //                                    IS_IGNORE_PARAMETER);
    // curr_green_gain = is_SetHardwareGain(camera_handle, IS_IGNORE_PARAMETER, 
    //                                      IS_IGNORE_PARAMETER, IS_GET_GREEN_GAIN,
    //                                      IS_IGNORE_PARAMETER);
    // curr_blue_gain = is_SetHardwareGain(camera_handle, IS_IGNORE_PARAMETER, 
    //                                     IS_IGNORE_PARAMETER, 
    //                                     IS_IGNORE_PARAMETER, IS_GET_BLUE_GAIN);
    // if (verbose) {
    //     printf("|\tGain: master, red, green, blue = %i, %i, %i, %i |\n", 
    //            curr_master_gain, curr_red_gain, curr_green_gain, curr_blue_gain);
    // }

    // Set initial gain to default struct value
    if (setMonoAnalogGain(all_camera_params.gainfact) < 0) {
        return -1;
    }
 
    // // disable camera's auto gain (0 = disabled)
    // if (is_SetAutoParameter(camera_handle, IS_SET_ENABLE_AUTO_GAIN, &ag, NULL) 
    //     != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error disabling auto gain: %s.\n", cam_error);
    //     return -1;
    // }
    // // check auto gain is off
    // is_SetAutoParameter(camera_handle, IS_GET_ENABLE_AUTO_GAIN, &curr_ag, NULL);
    // if (verbose) {
    //     printf("|\tAuto gain (should be disabled): %.1f\t\t  |\n", curr_ag);
    // }

    // All auto functions are off by default, controlled by autoexposure mode

    // // set camera's gamma value to off 
    // if (is_SetHardwareGamma(camera_handle, IS_SET_HW_GAMMA_OFF) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error disabling hardware gamma correction: %s.\n", cam_error);
    //     return -1;
    // }
    // // check hardware gamma is off
    // curr_gamma = is_SetHardwareGamma(camera_handle, IS_GET_HW_GAMMA);
    // if (verbose) {
    //     printf("|\tHardware gamma (should be off): %i\t\t  |\n", curr_gamma);
    // }

    // Set gamma to neutral. This is a formality, should already be 1.0
    double defaultGamma = 1.0;
    peak_status status = PEAK_STATUS_SUCCESS;
    peak_access_status accessStatus = peak_Gamma_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        status = peak_Gamma_Set(hCam, defaultGamma);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "Setting gamma failed. Continuing.\n");
        }
    }

    // // disable auto shutter
    // if (is_SetAutoParameter(camera_handle, IS_SET_ENABLE_AUTO_SHUTTER, 
    //                         &auto_shutter, NULL) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error disabling auto shutter: %s.\n", cam_error);
    //     return -1;
    // }
    // // check auto shutter is off
    // is_SetAutoParameter(camera_handle, IS_GET_ENABLE_AUTO_SHUTTER, 
    //                     &curr_shutter, NULL);
    // if (verbose) {
    //     printf("|\tAuto shutter (should be off): %.1f\t\t  |\n", curr_shutter);
    // }

    // All auto functions are off by default, controlled by autoexposure mode

    // // disable auto frame rate
    // if (is_SetAutoParameter(camera_handle, IS_SET_ENABLE_AUTO_FRAMERATE, &afr, 
    //                         NULL) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error disabling auto frame rate: %s.\n", cam_error);
    //     return -1;
    // }
    // // check auto frame rate is off
    // is_SetAutoParameter(camera_handle, IS_GET_ENABLE_AUTO_FRAMERATE, &auto_fr, 
    //                     NULL);
    // if (verbose) {
    //     printf("|\tAuto frame rate (should be off): %.1f\t\t  |\n", auto_fr);
    // }

    // All auto functions are off by default, controlled by autoexposure mode

    // // disable gain boost to reduce noise
    // if (is_SetGainBoost(camera_handle, IS_SET_GAINBOOST_OFF) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error disabling gain boost: %s.\n", cam_error);
    //     return -1;
    // }
    // // check gain boost is off
    // curr_gain_boost = is_SetGainBoost(camera_handle, IS_GET_GAINBOOST);
    // if (verbose) {
    //     printf("|\tGain boost (should be disabled): %i\t\t  |\n", 
    //            curr_gain_boost);	
    // }

    // Ensure we are not in autoexposure mode
    if (disableAutoExposure() < 0) {
        return -1;
    }

    // Ensure we are not in autogain mode
    if (disableAutoGain() < 0) {
        return -1;
    }

    // Newer sensors adhere to the analog/digital gain separation paradigm, not
    // "gain boost"

    // // turn off auto black level
    // blm = IS_AUTO_BLACKLEVEL_OFF;
    // if (is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_SET_MODE, (void *) &blm, 
    //                   sizeof(blm)) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error turning off auto black level mode: %s.\n", cam_error);
    //     return -1;
    // }
    // // check auto black level is off
    // bl_mode = is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_GET_MODE, 
    //                         (void *) &bl_mode, sizeof(bl_mode));
    // if (verbose) {
    //     printf("|\tAuto black level (should be off): %i\t\t  |\n", bl_mode);
    // }

    // Ensure black level will stay where we set it
    if (disableAutoBlackLevel() < 0) {
        return -1;
    }

    // // set black level offset to 50
    // blo = 50;
    // if (is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_SET_OFFSET, 
    //                   (void *) &blo, sizeof(blo)) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting black level offset to 50: %s.\n", cam_error);
    //     return -1;
    // }
    // // check black level offset
    // is_Blacklevel(camera_handle, IS_BLACKLEVEL_CMD_GET_OFFSET, 
    //               (void *) &bl_offset, sizeof(bl_offset));
    // if (verbose) {
    //     printf("|\tBlack level offset (desired is 50): %i\t\t  |\n", bl_offset);
    // }

    // Black level should be set after the bit depth in manual black level mode
    // Set black level to 0. Ideally this is chosen based on exposure conditions
    // to ensure the histogram is not clipped at 0.
    double defaultBlackLevelOffset = 0.0;
    if (setBlackLevelOffset(defaultBlackLevelOffset) < 0) {
        return -1;
    }

    // // This for some reason actually affects the time it takes to set aois only 
    // // on the focal plane camera. Don't use if for the trigger timeout. 
    // if (is_SetTimeout(camera_handle, IS_TRIGGER_TIMEOUT, 500) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting trigger timeout: %s.\n", cam_error);
    //     return -1;
    // }
    // // check time out
    // is_GetTimeout(camera_handle, IS_TRIGGER_TIMEOUT, &curr_timeout);
    // if (verbose) {
    //     printf("|\tCurrent trigger timeout: %i\t\t\t  |\n", curr_timeout);
    // }

    // Does not exist in iDS peak, also unclear from comments why this was a
    // problem

    return 1;
}
#endif


#ifndef IDS_PEAK
/* Function to load the camera at the beginning of the Star Camera session.
** Input: None.
** Output: A flag indicating successful loading of the camera or not.
*/
int loadCamera(void)
{
    int color_depth, pixelclock;
    void * active_mem_loc;
    double fps;

    // initialize camera
    if (is_InitCamera(&camera_handle, NULL) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error initializing camera in loadCamera(): %s.\n", cam_error);
        return -1;
    }
  
    // get sensor info
    if (is_GetSensorInfo(camera_handle, &sensorInfo) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error getting camera sensor information: %s.\n", cam_error);
        return -1;
    } 

    // set various other camera parameters
    if (setCameraParams() < 0) {
        return -1;
    }

    // set display mode and then get it to verify
    if (is_SetColorMode(camera_handle, IS_CM_MONO12) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting color mode: %s.\n", cam_error);
        return -1;
    }
    curr_color_mode = is_SetColorMode(camera_handle, IS_GET_COLOR_MODE);
    if (verbose) {
        printf("|\tCamera model: %s\t\t\t  |\n", sensorInfo.strSensorName);
        printf("|\tSensor ID/type: %i\t\t\t\t  |\n", sensorInfo.SensorID);
        printf("|\tSensor color mode: %i / %i\t\t\t  |\n", 
               sensorInfo.nColorMode, curr_color_mode);
        printf("|\tMaximum image width & height: %i, %i\t  |\n", 
               sensorInfo.nMaxWidth, sensorInfo.nMaxHeight);
        printf("|\tPixel size (micrometers): %.2f\t\t\t  |\n", 
              ((double) sensorInfo.wPixelSize)/100.0);
    }
     
    // allocate camera memory
    color_depth = 16; 
    if (is_AllocImageMem(camera_handle, sensorInfo.nMaxWidth, 
                         sensorInfo.nMaxHeight, color_depth, (char **) &mem_starting_ptr, 
                         &mem_id) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error allocating image memory: %s.\n", cam_error);
        return -1;
    }

    // set memory for image (make memory pointer active)
    if (is_SetImageMem(camera_handle, (char *)mem_starting_ptr, mem_id) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting image memory: %s.\n", cam_error);
        return -1;
    }

    // get image memory
    if (is_GetImageMem(camera_handle, &active_mem_loc) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error getting image memory: %s.\n", cam_error);
        return -1;
    }

    // set frame rate
    fps = 10;
    if (is_SetFrameRate(camera_handle, IS_GET_FRAMERATE, (void *) &fps) 
        != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting frame rate: %s.\n", cam_error);
        return -1;
    }

    pixelclock = 25;
    // Check pixelclock allowed values, in case settings changes have caused it
    // to differ
    unsigned int nRange[3] = {0};
    int nRet = is_PixelClock(camera_handle, IS_PIXELCLOCK_CMD_GET_RANGE, (void*)nRange, sizeof(nRange));
    if (nRet == IS_SUCCESS)
    {
        unsigned int nMin = nRange[0];
        unsigned int nMax = nRange[1];
        unsigned int nInc = nRange[2];
        printf("|\tPixelclock min, max, inc: %d, %d, %d  \t\t |\n", nMin, nMax, nInc);
        
        pixelclock = nMin;
    }

    // how clear images can be is affected by pixelclock and fps 
    if (is_PixelClock(camera_handle, IS_PIXELCLOCK_CMD_SET, 
                      (void *) &pixelclock, sizeof(pixelclock)) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting pixel clock: %s.\n", cam_error);
        return -1;
    }
    // get current pixel clock to check
    is_PixelClock(camera_handle, IS_PIXELCLOCK_CMD_GET, (void *) &curr_pc, 
                  sizeof(curr_pc));
    if (verbose) {
        printf("|\tPixel clock: %i\t\t\t\t\t  |\n", curr_pc);
    }

    // set trigger to software mode (call is_FreezeVideo to take single picture 
    // in single frame mode)
    if (is_SetExternalTrigger(camera_handle, IS_SET_TRIGGER_SOFTWARE) 
        != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error setting external trigger mode: %s.\n", cam_error);
        return -1;
    }
    // get the current trigger setting
    curr_ext_trig = is_SetExternalTrigger(camera_handle, IS_GET_EXTERNALTRIGGER);
    if (verbose) {
        printf("|\tCurrent external trigger mode: %i\t\t  |\n", curr_ext_trig);
    }

    return 1;
}
#else
int loadCamera(void)
{
    // // initialize camera
    // if (is_InitCamera(&camera_handle, NULL) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error initializing camera in loadCamera(): %s.\n", cam_error);
    //     return -1;
    // }

    // Init Library
    peak_status status = PEAK_STATUS_SUCCESS;
    peak_access_status accessStatus = PEAK_ACCESS_INVALID;

    printf("Initializing Library...\n");
    status = peak_Library_Init();
    if (!checkForSuccess(status)) {
        fprintf(stderr, "Initializing the library failed. Exiting.\n");
        return -1;
    }

    // Update camera list
    status = peak_CameraList_Update(NULL);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "Could not query camera list. Exiting.\n");
        return -1;
    }
    
    // Open first available camera
    status = peak_Camera_OpenFirstAvailable(&hCam);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Could not open camera. Exiting.\n");
        return -1;
    }

    // Reset camera to default
    status = peak_Camera_ResetToDefaultSettings(hCam);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Resetting camera parameters failed. Exiting.\n");
        return -1;
    }

    // Get camera name and update metadata struct
    peak_camera_id cameraId = PEAK_INVALID_CAMERA_ID;
    cameraId = peak_Camera_ID_FromHandle(hCam);
    accessStatus = peak_Camera_GetAccessStatus(cameraId);
    if (PEAK_IS_READABLE(accessStatus)) {
        peak_camera_descriptor cameraDescriptor = {};
        status = peak_Camera_GetDescriptor(cameraId, &cameraDescriptor);
        if(!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Failed to get camera model name.\n");
        } else {
            snprintf(default_metadata.detector, sizeof(cameraDescriptor.modelName), "%s",
                cameraDescriptor.modelName);
        }
    }
    default_metadata.sensorid = (uint64_t)cameraId;

    // NOTE(evanmayer): This logging is handled in FITS metadata now
    // // get sensor info
    // if (is_GetSensorInfo(camera_handle, &sensorInfo) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error getting camera sensor information: %s.\n", cam_error);
    //     return -1;
    // }

    // // set display mode and then get it to verify
    // if (is_SetColorMode(camera_handle, IS_CM_MONO12) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting color mode: %s.\n", cam_error);
    //     return -1;
    // }
    // curr_color_mode = is_SetColorMode(camera_handle, IS_GET_COLOR_MODE);
    // if (verbose) {
    //     printf("|\tCamera model: %s\t\t\t  |\n", sensorInfo.strSensorName);
    //     printf("|\tSensor ID/type: %i\t\t\t\t  |\n", sensorInfo.SensorID);
    //     printf("|\tSensor color mode: %i / %i\t\t\t  |\n", 
    //            sensorInfo.nColorMode, curr_color_mode);
    //     printf("|\tMaximum image width & height: %i, %i\t  |\n", 
    //            sensorInfo.nMaxWidth, sensorInfo.nMaxHeight);
    //     printf("|\tPixel size (micrometers): %.2f\t\t\t  |\n", 
    //           ((double) sensorInfo.wPixelSize)/100.0);
    // }

    // Set pixel format to unpacked Mono12
    accessStatus = peak_PixelFormat_GetAccessStatus(hCam);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        status = peak_PixelFormat_Set(hCam,
            PEAK_PIXEL_FORMAT_MONO12);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Setting pixel format failed. Exiting.\n");
            return -1;
        }
    }
    default_metadata.bitdepth = 12;

    // set various other camera parameters
    // black level is set inside, so do this after specifying pixel format
    if (setCameraParams() < 0) {
        return -1;
    }
    
    // // allocate camera memory
    // color_depth = 16; 
    // if (is_AllocImageMem(camera_handle, sensorInfo.nMaxWidth, 
    //                      sensorInfo.nMaxHeight, color_depth, (char **) &mem_starting_ptr, 
    //                      &mem_id) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error allocating image memory: %s.\n", cam_error);
    //     return -1;
    // }

    // // set memory for image (make memory pointer active)
    // if (is_SetImageMem(camera_handle, (char *)mem_starting_ptr, mem_id) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting image memory: %s.\n", cam_error);
    //     return -1;
    // }

    // // get image memory
    // if (is_GetImageMem(camera_handle, &active_mem_loc) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error getting image memory: %s.\n", cam_error);
    //     return -1;
    // }

    // Image memory handling is done automatically in iDS peak. We get frame
    // handles instead.

    // // set frame rate
    // fps = 10;
    // if (is_SetFrameRate(camera_handle, IS_GET_FRAMERATE, (void *) &fps) 
    //     != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting frame rate: %s.\n", cam_error);
    //     return -1;
    // }

    // It's not strictly necessary to set the acquisition framerate, as it only
    // applies in FreeRun mode. Also, the range of allowed values depend on and
    // will be overridden by the exposure time setting.
    setFps(10.0);

    // pixelclock = 25;
    // // Check pixelclock allowed values, in case settings changes have caused it
    // // to differ
    // unsigned int nRange[3] = {0};
    // int nRet = is_PixelClock(camera_handle, IS_PIXELCLOCK_CMD_GET_RANGE, (void*)nRange, sizeof(nRange));
    // if (nRet == IS_SUCCESS)
    // {
    //     unsigned int nMin = nRange[0];
    //     unsigned int nMax = nRange[1];
    //     unsigned int nInc = nRange[2];
    //     printf("|\tPixelclock min, max, inc: %d, %d, %d  \t\t |\n", nMin, nMax, nInc);
        
    //     pixelclock = nMin;
    // }

    // // how clear images can be is affected by pixelclock and fps 
    // if (is_PixelClock(camera_handle, IS_PIXELCLOCK_CMD_SET, 
    //                   (void *) &pixelclock, sizeof(pixelclock)) != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting pixel clock: %s.\n", cam_error);
    //     return -1;
    // }
    // // get current pixel clock to check
    // is_PixelClock(camera_handle, IS_PIXELCLOCK_CMD_GET, (void *) &curr_pc, 
    //               sizeof(curr_pc));
    // if (verbose) {
    //     printf("|\tPixel clock: %i\t\t\t\t\t  |\n", curr_pc);
    // }

    if (setMinPixelClock() < 0) {
        return -1;
    }

    // // set trigger to software mode (call is_FreezeVideo to take single picture 
    // // in single frame mode)
    // if (is_SetExternalTrigger(camera_handle, IS_SET_TRIGGER_SOFTWARE) 
    //     != IS_SUCCESS) {
    //     cam_error = printCameraError();
    //     printf("Error setting external trigger mode: %s.\n", cam_error);
    //     return -1;
    // }
    // // get the current trigger setting
    // curr_ext_trig = is_SetExternalTrigger(camera_handle, IS_GET_EXTERNALTRIGGER);
    // if (verbose) {
    //     printf("|\tCurrent external trigger mode: %i\t\t  |\n", curr_ext_trig);
    // }

    // Set trigger to software mode and enable
    const peak_trigger_mode triggerMode = PEAK_TRIGGER_MODE_SOFTWARE_TRIGGER;
    accessStatus = peak_Trigger_Mode_GetAccessStatus(hCam, triggerMode);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        status = peak_Trigger_Mode_Set(hCam, triggerMode);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Setting trigger mode failed. Exiting.\n");
            return -1;
        }
        status = peak_Trigger_Enable(hCam, PEAK_TRUE);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Enabling trigger failed. Exiting.\n");
            return -1;
        }
    } else {
        fprintf(stderr, "ERROR: Setting trigger mode failed. Trigger mode not \
            writeable at this time. Exiting.\n");
        return -1;
    }

    return 1;
}
#endif


#ifndef IDS_PEAK
int getNumberOfCameras(int* pNumCams)
{
    if (is_GetNumberOfCameras(pNumCams) != IS_SUCCESS) {
        printf("Cannot get # of cameras connected to computer.\n");
        *pNumCams = 0;
        return -1;
    }
    return 0;
}
#else
int getNumberOfCameras(int* pNumCams) {
    printf("Getting length of camera list...\n");
    size_t cameraListLength = 0;
    peak_status status = peak_CameraList_Get(NULL, &cameraListLength);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "getNumberOfCameras: Getting the camera list length "
            "failed.\n");
        (void)peak_Library_Exit();
        return -1;
    }

    if (verbose) {
        printf("getNumberOfCameras: Number of connected cameras: %zu\n", cameraListLength);
    }
    return 0;
}
#endif


#ifndef IDS_PEAK
// save image for future reference
int saveImageToDisk(char* filename)
{
    int ret = 0;
    wchar_t wFilename[200];
    swprintf(wFilename, 200, L"%s", filename);
    ImageFileParams.pwchFileName = wFilename;
    if (is_ImageFile(camera_handle, IS_IMAGE_FILE_CMD_SAVE, 
                    (void *) &ImageFileParams,
                    sizeof(ImageFileParams)) != IS_SUCCESS) {
        const char * last_error_str = printCameraError();
        printf("Failed to save image to file %ls: %s\n", wFilename, last_error_str);
        ret = -1;
    }
    return ret;
}
#else
int saveImageToDisk(char* filename, peak_frame_handle hFrame)
{
    // int ret = 0;
    // ImageFileParams.pwchFileName = filename;
    // if (is_ImageFile(camera_handle, IS_IMAGE_FILE_CMD_SAVE, 
    //                 (void *) &ImageFileParams,
    //                 sizeof(ImageFileParams)) != IS_SUCCESS) {
    //     const char * last_error_str = printCameraError();
    //     printf("Failed to save image: %s\n", last_error_str);
    //     ret = -1;
    // }
    // return ret;
    if (verbose) {
        printf("saveImageToDisk: saving %s\n", filename);
    }

    int ret = 0;
    peak_imagewriter_handle image_writer_handle = PEAK_INVALID_HANDLE;
    peak_status status = peak_IPL_ImageWriter_Create(&image_writer_handle);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to create image writer.\n");
        ret = -1;
    }
    status = peak_IPL_ImageWriter_Format_Set(image_writer_handle,
        PEAK_IMAGEFILE_FORMAT_PNG);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to set image writer format.\n");
        ret = -1;
    }
    status = peak_IPL_ImageWriter_Compression_Set(image_writer_handle, 0);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to create image compression.\n");
        ret = -1;
    }
    status = peak_IPL_ImageWriter_Save(image_writer_handle, hFrame, filename);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to dump file %s to disk.\n", filename);
        ret = -1;
    }
    status = peak_IPL_ImageWriter_Destroy(image_writer_handle);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to create destroy image writer.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief save the unprocessed image to disk as a comrpessed FITS file.
 * @details this should be called AFTER submitting the image to astrometry,
 * in order to avoid solution latency to the flight control software.
 * 
 * @param pUnpackedImage pointer to array of image bytes
 * @return int -1 if failed, 0 otherwise
 */
int saveFITStoDisk(uint16_t* pUnpackedImage)
{
    int ret = 0;
    // Record file creation date as str
    time_t seconds = time(NULL);
    struct tm* tm_info;
    tm_info = gmtime(&seconds);
    // if it is a leap year, adjust tm_info accordingly before it is passed to 
    // calculations in lostInSpace
    if (isLeapYear(tm_info->tm_year)) {
        // if we are on Feb 29
        if (tm_info->tm_yday == 59) {
            // 366 days in a leap year
            tm_info->tm_yday++;
            // we are still in February 59 days after January 1st (Feb 29)
            tm_info->tm_mon -= 1;
            tm_info->tm_mday = 29;
        } else if (tm_info->tm_yday > 59) {
            tm_info->tm_yday++;
        }
    }
    // Copy the creation date into the metadata struct
    strftime(default_metadata.date, sizeof(default_metadata.date),
        "%Y-%m-%d_%H-%M-%S", tm_info);

    char FITSfilename[256] = "";
    // Create the output filename
    strftime(FITSfilename, sizeof(FITSfilename),
        "/home/starcam/Desktop/TIMSC/img/"
        "saved_image_%Y-%m-%d_%H-%M-%S.fits", tm_info);

    snprintf(default_metadata.filename, sizeof(default_metadata.filename),
        "%s", FITSfilename);

    // Write the FITS File
    int FITSstatus = writeImage(FITSfilename, pUnpackedImage, CAMERA_WIDTH,
        CAMERA_HEIGHT, &default_metadata);
    if (0 != FITSstatus) {
        fprintf(stderr, "ERROR: writeImage failed.\n");
        ret = -1;
    }
    return ret;
}
#endif


#ifndef IDS_PEAK
/**
 * @brief Encapsulates the call to trigger an image capture.
 * 
 * @return int status: -1 if failed, 0 otherwise
 */
int imageCapture(void)
{
    int ret = 0;
    if (verbose) {
        printf("\n> Taking a new image...\n\n");
    }
    if (is_FreezeVideo(camera_handle, IS_WAIT) != IS_SUCCESS) {
        const char * last_error_str = printCameraError();
        printf("Failed to capture new image: %s\n", last_error_str);
        ret = -1;
    }
    return ret;
}
#else
/**
 * @brief Encapsulates the call to trigger an image capture. No image
 * acquisition or frame transfer logic.
 * @return int status: -1 if failed, 0 otherwise
 */
int imageCapture()
{
    int ret = 0;
    // Trigger the acquisition of an image
    gettimeofday(&metadataTv, NULL);
    peak_status status = peak_Trigger_Execute(hCam);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Capture trigger failed.\n");
        ret = -1;
    }
    return ret;
}
#endif


#ifndef IDS_PEAK
/**
 * @brief Transfer the captured image from the allocated camera shared memory
 * to a 16-bit local buffer. This function encapsulates any bit unpacking
 * required to translate from the image capture format to the working format.
 * @details For real-time applications, it's recommended to allocate the
 * required memory for shuffling around the image data one time at
 * initialization, rather than dynamically, which could reduce determinism or
 * fail during operation.
 * 
 * @param pUnpackedImage pointer to destination memory for the unpacked image.
 * IT IS THE CALLER'S RESPONSIBILITY TO PROVIDE ADEQUATE MEMORY ALLOCATION FOR
 * THE UNPACKED IMAGE.
 * @return int status: -1 for failure, 0 otherwise.
 */
int imageTransfer(uint16_t* pUnpackedImage)
{
    int ret = 0;
    // get the image from memory: abuse GetActSeqBuf to give us the pointer to
    // ppcMemLast, a pointer to the pointer to the image memory that was last
    // used for capturing an image.
    char* pDummyMem; // Required for call, unused
    uint16_t* pLocalMem;
    if (is_GetActSeqBuf(camera_handle, &buffer_num, (char **) &pDummyMem, 
        (char **) &pLocalMem) != IS_SUCCESS) {
        cam_error = printCameraError();
        printf("Error retrieving the active image memory: %s.\n", cam_error);
        return -1;
    }

    unpack_mono12(pLocalMem, pUnpackedImage, CAMERA_NUM_PX);

    return 0;
}
#else


/**
 * @brief Transfer the captured image from the received camera frame buffer to a
 * 16-bit local buffer. This function encapsulates any bit unpacking required to
 * translate from the image capture format to the working format.
 * 
 * @param pUnpackedImage pointer to destination memory for the unpacked image.
 * IT IS THE CALLER'S RESPONSIBILITY TO PROVIDE ADEQUATE MEMORY ALLOCATION FOR
 * THE UNPACKED IMAGE.
 * @return int status: -1 for failure, 0 otherwise.
 */
int imageTransfer(uint16_t* pUnpackedImage)
{
    solveState = IMAGE_XFER;
    int ret = 0;
    peak_frame_handle hFrame = PEAK_INVALID_HANDLE;
    double actualExpTimeMs = 1000.0; // if get fails, we'll wait 3s
    getExposureTime(&actualExpTimeMs);
    uint32_t three_frame_times_timeout_ms = (uint32_t)(3000.0 * actualExpTimeMs + 0.5);

    if (verbose) {
        printf("imageTransfer: Waiting for frame...\n");
    }

    // ---------------------------------------------------------------------- //
    // Actual data transfer
    // ---------------------------------------------------------------------- //
    // wait for image transfer
    peak_status status = peak_Acquisition_WaitForFrame(hCam,
        three_frame_times_timeout_ms, &hFrame);
    if(status == PEAK_STATUS_TIMEOUT) {
        fprintf(stderr, "ERROR: WaitForFrame timed out after 3 frame times.\n");
        return -1;
    } else if(status == PEAK_STATUS_ABORTED) {
        fprintf(stderr, "ERROR: WaitForFrame aborted by camera.\n");
        return -1;
    } else if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: WaitForFrame failed.\n");
        return -1;
    }

    // At this point we successfully got a frame handle. We need to release it
    // when done!
    if(peak_Frame_IsComplete(hFrame) == PEAK_FALSE) {
        printf("WARNING: Incomplete frame transfer.\n");
    }

    if (verbose) {
        printf("imageTransfer: Got frame, unpacking...\n");
    }

    // get image from frame handle
    peak_buffer buffer = {NULL, 0, NULL};
    status = peak_Frame_Buffer_Get(hFrame, &buffer);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to get buffer from frame.\n");
        return -1;
    }

    // If we want more image data, like the timestamp in camera
    // time (since camera init), we could query here.
    // This is also where we'd enable "chunks" for image metadata collection.

    // For the Mono12 unpacked format, each pixel occupies two bytes, and we can
    // iterate after casting the memory pointer
    if (verbose) {
        printf("imageTransfer: Unpacking image bytes: %ld into local buffer of "
            "size %ld\n", buffer.memorySize,
            (sizeof(uint16_t) * CAMERA_NUM_PX));
    }

    unpack_mono12((uint16_t *)buffer.memoryAddress, pUnpackedImage,
        CAMERA_NUM_PX);

    // ---------------------------------------------------------------------- //
    // Metadata handling
    // ---------------------------------------------------------------------- //

    // Record the whole number and microsecond parts of the timestamp as the
    // time of observation start
    default_metadata.utcsec = (uint64_t)metadataTv.tv_sec;
    default_metadata.utcusec = (uint64_t)metadataTv.tv_usec;

    // TODO(evanmayer): ccdtemp, not sure if there's a way to do this in peak API

    // Focus and aperture commands update this member after the command is
    // issued. So it should be current, and we don't want to ask the lens again.
    default_metadata.focus = (int16_t)all_camera_params.focus_position;
    default_metadata.aperture = (int16_t)all_camera_params.current_aperture;

    status = peak_Frame_Release(hCam, hFrame);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Frame_Release failed.\n");
        ret = -1;
    }

    return ret;
}


/**
 * @brief Set the Binning Factor
 * @details In order to change binning, we must stop and re-start acquisition.
 * 
 * @param factor 
 * @return int -1 if failed, 0 otherwise
 */
int setBinningFactor(uint8_t factor)
{
    int ret = 0;
    peak_status status = PEAK_STATUS_SUCCESS;

    // Stop acquisition if ongoing.
    // If we fail to stop image acquisition, binning will fail, so fail AF.
    if (PEAK_TRUE == peak_Acquisition_IsStarted(hCam)) {
        status = peak_Acquisition_Stop(hCam);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Failed to stop image acquisition. Exiting "
                "anyway.\n");
            return -1;
        }
    }

    // If we stopped acquisition, but we still don't have access to binning,
    // attempt to restart acquisition but fail AF.
    peak_access_status accessStatus = peak_BinningManual_GetAccessStatus(hCam,
        PEAK_SUBSAMPLING_ENGINE_SENSOR);
    if (PEAK_IS_WRITEABLE(accessStatus)) {
        status = peak_BinningManual_Set(hCam, PEAK_SUBSAMPLING_ENGINE_SENSOR,
            (uint32_t)factor, (uint32_t)factor);
        // If we could set binning, but it failed, attempt to restart
        // acquisition but fail AF.
        if(!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: setBinningFactor: setting factor %d "
                "failed.\n", factor);
            // Re-start acquisition. If we ever fail to re-start acquisition,
            // exit.
            status = peak_Acquisition_Start(hCam, PEAK_INFINITE);
            if (!checkForSuccess(status)) {
                fprintf(stderr, "ERROR: Failed to start image acquisition. Exiting.\n");
                exit(EXIT_FAILURE);
            }
            return -1;
        }
    } else {
        fprintf(stderr, "ERROR: Setting binning factor failed. Binning factor "
            "not writeable at this time, %d.\n", accessStatus);
        // Re-start acquisition. If we ever fail to re-start acquisition, exit.
        status = peak_Acquisition_Start(hCam, PEAK_INFINITE);
        if (!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Failed to start image acquisition. Exiting.\n");
            exit(EXIT_FAILURE);
        }
        return -1;
    }

    // Re-start acquisition. If we ever fail to re-start acquisition, exit.
    status = peak_Acquisition_Start(hCam, PEAK_INFINITE);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Failed to start image acquisition. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    return 0;
}


/**
 * @brief set binning back to 1
 * 
 * @return int -1 if failed, 0 otherwise
 */
int restoreBinningFactor(void)
{
    int ret = 0;
    if (setBinningFactor(1U) < 0) {
        // normal operation depends on correct binning factor set
        fprintf(stderr, "FATAL ERROR: restoreBinningFactor: failed to return "
            "binning factor to 1.\n");
        ret = -1;
    }
    return ret;
}


/**
 * @brief Measure image sharpness using the peak IPL
 * 
 * @param pSharpness to double to store sharpness value
 * @return int status: -1 for failure, 0 otherwise.
 */
int measureSharpness(double* pSharpness)
{
    START(tstart);
    int ret = 0;
    peak_frame_handle hFrame = PEAK_INVALID_HANDLE;
    double actualExpTimeMs = 1000.0; // if get fails, we'll wait 3s
    getExposureTime(&actualExpTimeMs);
    uint32_t three_frame_times_timeout_ms = (uint32_t)(3000.0 * actualExpTimeMs + 0.5);

    if (verbose) {
        printf("measureSharpness: Waiting for frame...\n");
    }

    // ---------------------------------------------------------------------- //
    // Actual data transfer
    // ---------------------------------------------------------------------- //
    // wait for image transfer
    peak_status status = peak_Acquisition_WaitForFrame(hCam,
        three_frame_times_timeout_ms, &hFrame);
    if(status == PEAK_STATUS_TIMEOUT) {
        fprintf(stderr, "ERROR: WaitForFrame timed out after 3 frame times.\n");
        return -1;
    } else if(status == PEAK_STATUS_ABORTED) {
        fprintf(stderr, "ERROR: WaitForFrame aborted by camera.\n");
        return -1;
    } else if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: WaitForFrame failed.\n");
        return -1;
    }

    // At this point we successfully got a frame handle. We need to release it
    // when done!
    if(peak_Frame_IsComplete(hFrame) == PEAK_FALSE) {
        printf("WARNING: Incomplete frame transfer.\n");
    }

    if (verbose) {
        printf("imageTransfer: Got frame, unpacking...\n");
    }

    // Only unpack to global application image memory if there's a GUI client
    // who might receive it
    if (num_clients > 0) {
        // get image from frame handle
        peak_buffer buffer = {NULL, 0, NULL};
        status = peak_Frame_Buffer_Get(hFrame, &buffer);
        if(!checkForSuccess(status)) {
            fprintf(stderr, "ERROR: Failed to get buffer from frame.\n");
            return -1;
        }
        unpack_mono12((uint16_t *)buffer.memoryAddress, unpacked_image,
            CAMERA_NUM_PX / (CAMERA_FOCUS_BINFACTOR * CAMERA_FOCUS_BINFACTOR));
    }

    // measure sharpness
    uint32_t border = 4U; // blank pixels around active array
    peak_position offset = {
        .x = border,
        .y = border
    };
    peak_size size = {
        .width = CAMERA_WIDTH / CAMERA_FOCUS_BINFACTOR,
        .height = CAMERA_HEIGHT / CAMERA_FOCUS_BINFACTOR
    };
    peak_roi roi = {
        .offset = offset,
        .size = size
    };
    status = peak_ROI_Get(hCam, &roi);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: peak_ROI_Get failed.\n");
    }
    // PEAK_SHARPNESS_ALGORITHM_SOBEL
    // PEAK_SHARPNESS_ALGORITHM_TENENGRAD
    // PEAK_SHARPNESS_ALGORITHM_MEAN_SCORE
    // PEAK_SHARPNESS_ALGORITHM_HISTOGRAM_VARIANCE
    // required to avoid error with bad ROI size. Why is asking for the ROI we
    // just queried in error? God only knows
    roi.size.width = roi.size.width - border;
    roi.size.height = roi.size.height - border;
    status = peak_IPL_Sharpness_Measure(hFrame, roi,
        PEAK_SHARPNESS_ALGORITHM_SOBEL, pSharpness);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: peak_IPL_Sharpness_Measure failed.\n");
        ret = -1;
    }

    status = peak_Frame_Release(hCam, hFrame);
    if(!checkForSuccess(status)) {
        fprintf(stderr, "ERROR: Frame_Release failed.\n");
        ret = -1;
    }
    STOP(tend);
    DISPLAY_DELTA("sharpness time", DELTA(tend, tstart));

    return ret;
}


/**
 * @brief Ensures peak IPL hot pixel compensation is on, sets it to maximum, and
 * re-makes the hot pixel list.
 * 
 * @return int -1 if failed, 0 otherwise
 */
int renewCameraHotPixels(void)
{
    START(tstart);
    if (verbose) {
        printf("renewCameraHotPixels: making new internal camera hot pixel "
            "mask.\n");
    }
    // Enable hardware hot pixel compensation
    peak_status status = peak_IPL_HotpixelCorrection_Enable(hCam, PEAK_TRUE);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "renewCameraHotPixels: Enabling hot pixel correction"
            " failed.\n");
        return -1;
    }
    // Set sensitivity level. This also resets the hot pixel list, triggering
    // the camera to re-find hot pixels.
    peak_hotpixel_correction_sensitivity sens = PEAK_HOTPIXEL_CORRECTION_SENSITIVITY_LEVEL_5; // max
    status = peak_IPL_HotpixelCorrection_Sensitivity_Set(hCam, sens);
    if (!checkForSuccess(status)) {
        fprintf(stderr, "renewCameraHotPixels: Setting hot pixel correction "
            "sensitivity failed.\n");
        return -1;
    }
    STOP(tend);
    DISPLAY_DELTA("||||||||cam HP", DELTA(tend, tstart));
    return 0;
}
#endif


#ifndef IDS_PEAK
/* Function to establish the parameters for saving images taken by the camera.
** Input: None.
** Output: None (void).
*/
void setSaveImage()
{
    ImageFileParams.pwchFileName = L"save1.png"; // BMP not supported for >8bit
    ImageFileParams.pnImageID = NULL;
    ImageFileParams.ppcImageMem = NULL;
    ImageFileParams.nQuality = 100;
    ImageFileParams.nFileType = IS_IMG_PNG;
}
#endif


/* Function to mask hot pixels accordinging to static and dynamic maps.
** Input: The image bytes (ib), the image border indices (i0, j0, i1, j1), rest 
** are 0.
** Output: None (void). Makes the dynamic and static hot pixel masks for the 
** Star Camera image.
*/
void makeMask(uint16_t * ib, int i0, int j0, int i1, int j1, int x0, int y0, 
              bool subframe)
{
    static int first_time = 1;
    static int * x_p = NULL, * y_p = NULL;
    static int num_p = 0, num_alloc = 0;

    if (first_time) {
        mask = calloc(CAMERA_NUM_PX, 1);
        x_p = calloc(100, sizeof(int));
        y_p = calloc(100, sizeof(int));
        num_alloc = 100;
    }

    // load static hot pixel mask
    if (first_time || all_blob_params.make_static_hp_mask) {
        // read from file with bright pixel coordinates in it
        FILE * f = fopen(STATIC_HP_MASK, "r");
        int i, j;
        num_p = 0;

        if (f) {
            if (verbose) {
                printf("\n+---------------------------------------------------------+\n");
                printf("|\t\tLoading static hot pixel map...\t\t  |\n");
                printf("|---------------------------------------------------------|\n");
            }

            char * line = NULL;
            size_t len = 0;
            int read = 0;

            // read every line in the file
            while ((read = getline(&line, &len, f)) != -1) {
                sscanf(line, "%d,%d\n", &i, &j);

                if (num_p >= num_alloc) {
                    num_alloc += 100;
                    x_p = realloc(x_p, sizeof(int) * num_alloc);
                    y_p = realloc(y_p, sizeof(int) * num_alloc);
                }

                x_p[num_p] = i;
                // map y coordinate to image in memory from Kst blob
                y_p[num_p] = CAMERA_HEIGHT - j; 

                // TODO(evanmayer) figure out a less annoying way to report this
                // if (verbose) {
                //     printf("|\tCoordinates read from hp file: [%4i, %4i]\t  |\n", 
                //            i, j);
                // }

                num_p++;
            }
            
            if (verbose) {
                printf("+---------------------------------------------------------+\n");
            }

            fflush(f);
            fclose(f);
            free(line);
        }

        // do not want to recreate hp mask automatically, so set field to 0
        all_blob_params.make_static_hp_mask = 0;
        // no longer the first time we are running
        first_time = 0;
    }

    int i, j;
    int p0, p1, p2, p3, p4;
    int a, b;
    
    int cutoff = all_blob_params.spike_limit*100.0;

    // Zero out borders of mask array
    for (i = i0; i < i1; i++) {
        mask[i + CAMERA_WIDTH*j0] = mask[i + (j1-1)*CAMERA_WIDTH] = 0;
    }

    for (j = j0; j < j1; j++) {
        mask[i0 + j*CAMERA_WIDTH] = mask[i1 - 1 + j*CAMERA_WIDTH] = 0;
    }

    i0++;
    j0++;
    i1--;
    j1--;
  
    if (all_blob_params.dynamic_hot_pixels) {
        int nhp = 0;

        for (j = j0; j < j1; j++) {
            for (i = i0; i < i1; i++) {
                // pixels left/right, above/below
                p0 = 100*ib[i + j*CAMERA_WIDTH]/cutoff;
                p1 = ib[i - 1 + (j)*CAMERA_WIDTH];
                p2 = ib[i + 1 + (j)*CAMERA_WIDTH];
                p3 = ib[i + (j+1)*CAMERA_WIDTH];
                p4 = ib[i + (j-1)*CAMERA_WIDTH];
                a = p1 + p2 + p3 + p4 + 4;
                // pixels on diagonal (upper left/right, lower left/right)
                p1 = ib[i - 1 + (j-1)*CAMERA_WIDTH];
                p2 = ib[i + 1 + (j+1)*CAMERA_WIDTH];
                p3 = ib[i - 1 + (j+1)*CAMERA_WIDTH];
                p4 = ib[i + 1 + (j-1)*CAMERA_WIDTH];
                b = p1 + p2 + p3 + p4 + 4;
                mask[i + j*CAMERA_WIDTH] = ((p0 < a) && (p0 < b));
                if (p0 > a || p0 > b) nhp++;
            }
        }

        if (verbose) {
            printf("\n(*) Number of hot pixels found: %d.\n\n", nhp);
        }
    } else {
        for (j = j0; j < j1; j++) {
            for (i = i0; i < i1; i++) {
                mask[i + j*CAMERA_WIDTH] = 1;
            }
        }
    }

    if (all_blob_params.use_static_hp_mask) {
        if (verbose) {
            printf("+---------------------------------------------------------+\n");
            printf("|\t\tMasking %d pixels...\t\t\t  |\n", num_p);
            printf("|---------------------------------------------------------|\n");
        }

        for (int i = 0; i < num_p; i++) {
            // set all static hot pixels to 0
            // if (verbose) {
            //     printf("|\tCoordinates going into mask: [%4i, %4i]\t  |\n", 
            //            x_p[i], CAMERA_HEIGHT - y_p[i]);
            // }

            int ind = CAMERA_WIDTH * y_p[i] + x_p[i];
            mask[ind] = 0;
        }

        if (verbose) {
            printf("+---------------------------------------------------------+\n");
        }
    }
}


/* Function to process the image with a filter to reduce noise.
** Input:
** Output:
*/

/**
 * @brief Function that will do a simple boxcar smoothing of an image.
 * 
 * @param ib "input buffer" the input image with 12 bit depth, stored in 16bit ints
 * @param i0 starting column for filtering
 * @param j0 starting row for filtering
 * @param i1 ending column for filtering
 * @param j1 ending row for filtering
 * @param r_f boxcar filter radius
 * @param filtered_image output image
 */
void boxcarFilterImage(uint16_t * ib, int i0, int j0, int i1, int j1, int r_f, 
                       double * filtered_image)
{
    static int first_time = 1;
    static char * nc = NULL;
    static uint64_t * ibc1 = NULL;

    if (first_time) {
        nc = calloc(CAMERA_NUM_PX, 1);
        ibc1 = calloc(CAMERA_NUM_PX, sizeof(uint64_t));
        first_time = 0;
    }

    int b = r_f;
    int64_t isx;
    int s, n;
    double ds, dn;
    double last_ds = 0;

    for (int j = j0; j < j1; j++) {
        n = 0;
        isx = 0;
        for (int i = i0; i < i0 + 2*r_f + 1; i++) {
            n += mask[i + j*CAMERA_WIDTH];
            isx += ib[i + j*CAMERA_WIDTH]*mask[i + j*CAMERA_WIDTH];
        }

        int idx = CAMERA_WIDTH*j + i0 + r_f;

        for (int i = r_f + i0; i < i1 - r_f - 1; i++) {
            ibc1[idx] = isx;
            nc[idx] = n;
            isx = isx + mask[idx + r_f + 1]*ib[idx + r_f + 1] - 
                  mask[idx - r_f]*ib[idx - r_f];
            n = n + mask[idx + r_f + 1] - mask[idx - r_f];
            idx++;
        }

        ibc1[idx] = isx;
        nc[idx] = n;
    }

    for (int j = j0+b; j < j1-b; j++) {
        for (int i = i0+b; i < i1-b; i++) {
            n = s = 0;
            for (int jp =- r_f; jp <= r_f; jp++) {
                int idx = i + (j+jp)*CAMERA_WIDTH;
                s += ibc1[idx];
                n += nc[idx];
            }
            ds = s;
            dn = n;
            if (dn > 0.0) {
                ds /= dn;
                last_ds = ds;
            } else {
                ds = last_ds;
            }
            filtered_image[i + j*CAMERA_WIDTH] = ds;
        }
    }
}


/* Function to find the blobs in an image.
** Inputs: The original image prior to processing (input_biffer), the dimensions
** of the image (w & h) pointers to arrays for the x coordinates, y coordinates,
** and magnitudes (pixel values) of the blobs, and an array for the bytes of the
** image after processing (masking, filtering, et cetera).
** Output: the number of blobs detected in the image.
*/
int findBlobs(uint16_t * input_buffer, int w, int h, double ** star_x, 
              double ** star_y, double ** star_mags, uint16_t * output_buffer)
{
    static int first_time = 1;
    static double * ic = NULL, * ic2 = NULL;
    static int num_blobs_alloc = 0;
    FILE *fp;
    // test code to grab real filtered images if we want.
    // fp = fopen("/home/starcam/filtered.txt","w");

    // allocate the proper amount of storage space to start
    if (first_time) {
        ic = calloc(CAMERA_NUM_PX, sizeof(double));
        ic2 = calloc(CAMERA_NUM_PX, sizeof(double));
        first_time = 0;
    }
  
    // we use half-width internally, but the API gives us full width.
    int x_size = w/2;
    int y_size = h/2;

    int j0, j1, i0, i1;
    // extra image border
    int b = 0;

    j0 = i0 = 0;
    j1 = h;
    i1 = w;
    
    b = all_blob_params.centroid_search_border;

    solveState = HOTPIX_MASK;
    // if we want to make a new hot pixel mask
    if (all_blob_params.make_static_hp_mask) {
        if (verbose) {
            printf("**************************** Making static hot pixel map..."
                   "****************************\n");
        }

        // make a file and write bright pixel coordinates to it
        FILE * f = fopen(STATIC_HP_MASK, "w"); 

        for (int yp = 0; yp < CAMERA_HEIGHT; yp++) {
            for (int xp = 0; xp < CAMERA_WIDTH; xp++) {
                // index in the array where we are
                int ind = CAMERA_WIDTH*yp + xp; 
                // check pixel value to see if it's above hot pixel threshold
                if (input_buffer[ind] > all_blob_params.make_static_hp_mask) {
                    int i = xp;
                    // make this agree with blob coordinates in Kst
                    int j = CAMERA_HEIGHT - yp; 
                    fprintf(f, "%d,%d\n", i, j);
                    if (verbose) {
                        printf("Value of pixel (should be > %i): %d | "
                               "Coordinates: [%d, %d]\n", 
                               all_blob_params.make_static_hp_mask, 
                               input_buffer[ind], i, j);
                    }
                }
            }
        }

        fflush(f);
        fclose(f);
    }

    makeMask(input_buffer, i0, j0, i1, j1, 0, 0, 0);

    double sx = 0, sx2 = 0;
    // sum of non-highpass filtered field
    double sx_raw = 0;                            
    int num_pix = 0;

    solveState = FILTERING;
    // lowpass filter the image - reduce noise.
    boxcarFilterImage(input_buffer, i0, j0, i1, j1, all_blob_params.r_smooth, 
                      ic);
    // test code to grab real filtered images if we want.
    /* for (int j = 0; j < CAMERA_HEIGHT; j++)
    {
        for (int i = 0; i < CAMERA_WIDTH; i++)
        {   
            int ind = CAMERA_WIDTH*j + i;
            fprintf(fp, "%lf", ic[ind]);
            if (i == CAMERA_WIDTH-1)
            {
                fprintf(fp,"%s","\n");
            } else {
                fprintf(fp,"%s",",");
            }
            
        }
        
    }
    
    fflush(fp);
    fclose(fp); */


    // only high-pass filter full frames
    if (all_blob_params.high_pass_filter) {
        b += all_blob_params.r_high_pass_filter;

        boxcarFilterImage(input_buffer, i0, j0, i1, j1, 
                          all_blob_params.r_high_pass_filter, ic2);

        for (int j = j0+b; j < j1-b; j++) {
            for (int i = i0+b; i < i1-b; i++) {
                int idx = i + j*w;
                sx_raw += ic[idx]*mask[idx];
                ic[idx] -= ic2[idx];
                sx += ic[idx]*mask[idx];
                sx2 += ic[idx]*ic[idx]*mask[idx];
                num_pix += mask[idx];
            }
        }
    } else {
        for (int j = j0+b; j < j1-b; j++) {
            for (int i = i0+b; i < i1-b; i++) {
                int idx = i + j*w;
                sx += ic[idx]*mask[idx];
                sx2 += ic[idx]*ic[idx]*mask[idx];
                num_pix += mask[idx];
            }
        }
        sx_raw = sx;
    }

    double mean = sx/num_pix;
    double mean_raw = sx_raw/num_pix;
    double sigma = sqrt((sx2 - sx*sx/num_pix)/num_pix);
    if (verbose) {
        printf("\n+---------------------------------------------------------+\n");
        printf("|\t\tBlob-finding calculations\t\t  |\n");
        printf("|---------------------------------------------------------|\n");
        printf("|\tMean = %f\t\t\t\t\t  |\n", mean);
        printf("|\tSigma = %f\t\t\t\t  |\n", sigma);
        printf("|\tRaw mean = %f\t\t\t\t  |\n", mean_raw);
        printf("+---------------------------------------------------------+\n");
    }

    // fill output buffer if the variable is defined 
    if (output_buffer) {
        int pixel_offset = 0;

        if (all_blob_params.high_pass_filter) pixel_offset = 50;

        if (all_blob_params.filter_return_image) {
            if (verbose) {
                printf("\nFiltering returned image...\n");
            }

            for (int j = j0 + 1; j < j1 - 1; j++) {
                for (int i = i0 + 1; i < i1 - 1; i++) {
                  output_buffer[i + j*w] = ic[i + j*w]+pixel_offset;
                }
            }

            for (int j = 0; j < b; j++) {
                for (int i = i0; i < i1; i++) {
                  output_buffer[i + (j + j0)*w] = 
                  output_buffer[i + (j1 - j - 1)*w] = mean + pixel_offset;
                }
            }

            for (int j = j0; j < j1; j++) {
                for (int i = 0; i < b; i++) {
                  output_buffer[i + i0 + j*w] = 
                  output_buffer[i1 - i - 1 + j*w] = mean + pixel_offset;
                }
            }
        } else {
            if (verbose) {
                printf("\n> Not filtering the returned image...\n\n");
            }

            for (int j = j0; j < j1; j++) {
                for (int i = i0; i < i1; i++) {
                  int idx = i + j*w;
                  output_buffer[idx] = input_buffer[idx];
                }
            }
        }
    }

    // FIXME(evanmayer): In new sensor testing, I found that there was a rare
    // segfault in merge(). I think this might happen if a lot of blobs are
    // present (e.g. lens cap on image).
    // The recursive stack memory usage in merge() is my primary suspect.
    // Legit real-time code would preallocate the max size one would ever need
    // for this task, and then straight up fail gracefully or limit the number
    // of blobs used when hitting the limit.
    // Technically this could realloc until we run out, or force astrometry to
    // deal with thousands and thousands of fake blobs, which are way worse.

    // And really, when are we ever going to see thousands, or even hundreds of
    // stars? And are those extra few hundred stars going to improve
    // performance? No.

    solveState = BLOB_FIND;

    // find the blobs 
    double ic0;
    int blob_count = 0;
    for (int j = j0 + b + 1; j < j1-b-2; j++) {
        for (int i = i0 + b + 1; i < i1-b-2; i++) {
            // if pixel exceeds threshold
            if ((double) ic[i + j*w] > mean + all_blob_params.n_sigma*sigma) {
                ic0 = ic[i + j*w];
                // if pixel is a local maximum or saturated
                if (((ic0 >= ic[i-1 + (j-1)*w]) &&
                     (ic0 >= ic[i   + (j-1)*w]) &&
                     (ic0 >= ic[i+1 + (j-1)*w]) &&
                     (ic0 >= ic[i-1 + (j  )*w]) &&
                     (ic0 >  ic[i+1 + (j  )*w]) &&
                     (ic0 >  ic[i-1 + (j+1)*w]) &&
                     (ic0 >  ic[i   + (j+1)*w]) &&
                     (ic0 >  ic[i+1 + (j+1)*w])) ||
                     (ic0 > (CAMERA_MAX_PIXVAL - 1))) {

                    int unique = 1;

                    // realloc array if necessary (when the camera looks at a 
                    // really bright image, this slows everything down severely)
                    if (blob_count >= num_blobs_alloc) {
                        num_blobs_alloc += 500;
                        *star_x = realloc(*star_x, sizeof(double)*num_blobs_alloc);
                        *star_y = realloc(*star_y, sizeof(double)*num_blobs_alloc);
                        *star_mags = realloc(*star_mags, sizeof(double)*num_blobs_alloc);
                    }

                    (*star_x)[blob_count] = i;
                    (*star_y)[blob_count] = j;
                    (*star_mags)[blob_count] = 100*ic[i + j*CAMERA_WIDTH];

                    // FIXME: not sure why this is necessary..
                    if ((*star_mags)[blob_count] < 0) {
                        (*star_mags)[blob_count] = UINT32_MAX;
                    }

                    // if we already found a blob within SPACING and this one is
                    // bigger, replace it.
                    int spacing = all_blob_params.unique_star_spacing;
                    if ((*star_mags)[blob_count] > 25400) {
                        spacing = spacing * 4;
                    }
                    for (int ib = 0; ib < blob_count; ib++) {
                        if ((abs((*star_x)[blob_count]-(*star_x)[ib]) < spacing) &&
                            (abs((*star_y)[blob_count]-(*star_y)[ib]) < spacing)) {
                            unique = 0;
                            // keep the brighter one
                            if ((*star_mags)[blob_count] > (*star_mags)[ib]) {
                                (*star_x)[ib] = (*star_x)[blob_count];
                                (*star_y)[ib] = (*star_y)[blob_count];
                                (*star_mags)[ib] = (*star_mags)[blob_count];
                            }
                        }
                    }
                    // if we didn't find a close one, it is unique.
                    if (unique) {
                        blob_count++;
                    }
                }
            }
        }
    }
    // this loop flips vertical position of blobs back to their normal location
    for (int ibb = 0; ibb < blob_count; ibb++) {
        (*star_y)[ibb] = CAMERA_HEIGHT - (*star_y)[ibb];
    }

    // merge sort
    part(*star_mags, 0, blob_count - 1, *star_x, *star_y); 
    if (verbose) {
        printf("(*) Number of blobs found in image: %i\n\n", blob_count);
    }

    return blob_count;
}


/* Function for sorting implementation.
** Input: 
** Output:
*/
void merge(double * A, int p, int q, int r, double * X, double * Y)
{
    int n1 = q - p + 1, n2 = r - q;
    int lin = n1 + 1, rin = n2 + 1;
    double LM[lin], LX[lin], LY[lin], RM[rin], RX[rin], RY[rin];
    int i, j, k;
    LM[n1] = 0;
    RM[n2] = 0;

    for (i = 0; i < n1; i++) {
        LM[i] = A[p+i];
        LX[i] = X[p+i];
        LY[i] = Y[p+i];
    }

    for (j = 0; j < n2; j++) {
        RM[j] = A[q+j+1];
        RX[j] = X[q+j+1];
        RY[j] = Y[q+j+1];
    }

    i = 0; j = 0;
    for (k = p; k <= r; k++) {
        if (LM[i] >= RM[j] ){
            A[k] = LM[i];
            X[k] = LX[i];
            Y[k] = LY[i];
            i++;
        } else {
            A[k] = RM[j];
            X[k] = RX[j];
            Y[k] = RY[j];
            j++;
        }
    }
}


/* Recursive part of sorting.
** Input:
** Output:
*/
void part(double * A, int p, int r, double * X, double * Y)
{
    if (p < r) {
        int q = (p + r)/2;
        part(A, p, q, X, Y);
        part(A, q + 1, r, X, Y);
        merge(A, p, q, r, X, Y);
    }
}


#ifndef IDS_PEAK
/* Function to load saved images (likely from previous observing sessions, but 
** could also be test pictures).
** Inputs: the name of the image to be loaded (filename) and the active image 
** memory (buffer).
** Outputs: A flag indicating successful loading of the image or not.
*/
int loadDummyPicture(char* filename, char** buffer)
{
    ImageFileParams.ppcImageMem = buffer;
    ImageFileParams.pwchFileName = (wchar_t*)filename;
    ImageFileParams.ppcImageMem = NULL;
    if (is_ImageFile(camera_handle, IS_IMAGE_FILE_CMD_LOAD, 
                     (void *) &ImageFileParams, sizeof(ImageFileParams)) 
                     != IS_SUCCESS) {
        return -1;
    } else {
        return 1;
    }
    ImageFileParams.ppcImageMem = NULL;
}
#endif


/* Function to make table of stars from image for displaying in Kst (mostly for 
** testing).
** Inputs: The name of blob table file, array of blob magnitudes, array of blob 
** x coordinates, array of blob y coordinates, and number of blobs.
** Output: A flag indicating successful writing of the blob table file.
*/
int makeTable(char * filename, double * star_mags, double * star_x, 
              double * star_y, int blob_count)
{
    FILE * fp;

    if ((fp = fopen(filename, "w")) == NULL) {
        fprintf(stderr, "Could not open blob-writing table file: %s.\n", 
                strerror(errno));
        return -1;
    }

    for (int i = 0; i < blob_count; i++) {
        fprintf(fp, "%f,%f,%f\n", star_mags[i], star_x[i], star_y[i]);
    }

    // close both files and return
    fflush(fp);
    fclose(fp);
    return 1;
}


int doContrastDetectAutoFocus(struct camera_params* all_camera_params, struct tm* tm_info, uint16_t* output_buffer) {
    printf("Running contrast detection AF.\n");

    // Housekeeping
    static FILE * af_file = NULL;
    static char af_filename[256];

    // AF tracking
    uint16_t numFocusPos = 0;
    // A upper bound on focus tries guards against focusing forever
    // If someone accidentally orders an 8000-range, 5-step AF run or worse, we'll
    // cut out early
    uint16_t remainingFocusPos = 1600; 
    char focusStrCmd[10] = {'\0'};

    // Contrast detect algorithm parameters
    uint16_t kernelSize = 9;
    // We use the sobel Y kernel because we assume 2 things:
    // * the sensor x-axis is aligned with the horizon
    // * we scan primarily in azimuth, so any blurring will primarily be in x,
    //   making the Y-gradient a more reliable metric than x.
    float sobelKernelY[9] = {-1., -2., -1., 0., 0., 0., 1., 2., 1.};
    // astropy.convolution.Gaussian2DKernel(x_stddev=1.27, y_stddev=1.27, x_size=3, y_size=3),
    // matched filter for star PSF FWHM = 3 px, normalized kernel
    float gaussian4px[9] = {0.08839674, 0.12052241, 0.08839674, 0.12052241,
        0.16432339, 0.12052241, 0.08839674, 0.12052241, 0.08839674};

    // Initialize focuser and AF logging
    int bestFocusPos = all_camera_params->focus_position;
    double bestFocusSharpness = 0;

    all_camera_params->begin_auto_focus = 0;

    if (af_file != NULL) {
        fclose(af_file);
        af_file = NULL;
    }
    // clear previous contents of auto-focusing file (open in write mode)
    strftime(
        af_filename,
        sizeof(af_filename),
        "/home/starcam/Desktop/TIMSC/auto_focus_starting_%Y-%m-%d_%H-%M-%S.txt",
        tm_info
    );
    if (verbose) {
        printf("Opening auto-focusing text file: %s\n", af_filename);
    }
    if ((af_file = fopen(af_filename, "w")) == NULL) {
        fprintf(stderr, "Could not open auto-focusing file: %s.\n", 
                strerror(errno));
        all_camera_params->focus_mode = 0;
        return -1;
    }

    // check that end focus position is at least 25 less than max focus
    // position
    if (all_camera_params->max_focus_pos - all_camera_params->end_focus_pos 
        < 25) {
        printf("Adjusting end focus position to be 25 less than max focus "
                "position.\n");
        all_camera_params->end_focus_pos = all_camera_params->max_focus_pos 
                                            - 25;
    }
    // check that beginning focus position is at least 25 above min focus
    // position
    if (all_camera_params->start_focus_pos - all_camera_params->min_focus_pos
        < 25) {
        printf("Adjusting beginning focus position to be 25 more than min "
                "focus position.\n");
        all_camera_params->start_focus_pos = all_camera_params->min_focus_pos
                                            + 25;
    }
    // get to beginning of auto-focusing range
    if (beginAutoFocus() < 1) {
        printf("Error beginning auto-focusing process. Skipping to taking "
                "observing images...\n");
        // return to default focus position
        if (defaultFocusPosition() < 1) {
            printf("Error moving to default focus position.\n");
            all_camera_params->focus_mode = 0;
            return -1;
        }

        // abort auto-focusing process
        all_camera_params->focus_mode = 0;
    }

    // Loop until all focus positions covered
    bool hasGoneForward = 0;
    bool hasGoneBackward = 0;
    bool atFarEnd = 0;
    bool atNearEnd = 0;
    int dir = 1;

    // Set binning to speed up sharpness measurement
    // NOTE: any return statements between here and the end of the focusing loop
    // must be guarded by a call to attempt to return the bin factor to 1.
    // (restoreBinningFactor())
    if (setBinningFactor(CAMERA_FOCUS_BINFACTOR) < 0) {
        // focusing ROI in measureSharpness depends on correct binning factor
        // set
        return -1;
    }

    while (remainingFocusPos > 0) {
        remainingFocusPos -= 1;
        atFarEnd = (all_camera_params->focus_position >= all_camera_params->end_focus_pos);
        atNearEnd = (all_camera_params->focus_position < all_camera_params->start_focus_pos);
        if (atFarEnd) {
            if (hasGoneForward && !hasGoneBackward) {
                dir = -1;
                hasGoneBackward = 1;
            }
        }
        if (atNearEnd && hasGoneBackward) {
            // Break out of AF
            all_camera_params->focus_mode = 0;
            printf("Quitting autofocus after fulfilling end conditions...\n");
            break;
        }
        if (0 == all_camera_params->focus_mode) {
            printf("Quitting autofocus by user cancel...\n");
            break;
        }

        taking_image = 1;
        if (imageCapture() < 0) {
            fprintf(stderr, "Could not complete image capture: %s.\n", 
                strerror(errno));
            all_camera_params->focus_mode = 0;
            if (restoreBinningFactor() < 0) {
                exit(EXIT_FAILURE);
            }
            return -1;
        }
        taking_image = 0;

        double sharpness = 0.0;
        if (measureSharpness(&sharpness) < 0) {
            fprintf(stderr, "Could not complete sharpness measurement: %s.\n", 
            strerror(errno));
            all_camera_params->focus_mode = 0;
            if (restoreBinningFactor() < 0) {
                exit(EXIT_FAILURE);
            }
            return -1;
        };

        // We don't save individual AF images in order to maximize speed,
        // thereby minimizing the possible change in image levels and star field
        // content across a run.

        // Save off commanded position and max gradient in image
        if (sharpness >= bestFocusSharpness) {
            bestFocusSharpness = sharpness;
            bestFocusPos = (int32_t)all_camera_params->focus_position;
        }

        // Save off data in AF logfile
        all_camera_params->flux = sharpness;

        if (verbose) {
            printf("(*) Sharpness metric in image for focus %d is %lf.\n",
            all_camera_params->focus_position, sharpness);
        }
        fprintf(af_file, "%.6lf\t%5d\n", sharpness,
                all_camera_params->focus_position);
        fflush(af_file);

        // if clients are listening and we want to guarantee data is sent to
        // them before continuing with auto-focusing, wait until data_sent
        // confirmation. If there are no clients, no need to slow down auto-
        // focusing
        send_data = 1;
        if (num_clients > 0) {
            while (!telemetry_sent) {
                if (verbose) {
                    printf("> Waiting for data to send to client...\n");
                }
                usleep(10000);
            }
        }
        send_data = 0;

        // Move to next position
        // if (all_camera_params->focus_position >= all_camera_params->end_focus_pos) {
        //     all_camera_params->focus_step *= -1;
        // } // two-way AF
        // int focusStep = min(
        //     all_camera_params->focus_step,
        //     all_camera_params->end_focus_pos -
        //     all_camera_params->focus_position);
        int focusStep = all_camera_params->focus_step * dir;
        sprintf(focusStrCmd, "mf %i\r", focusStep);
        if (!cancelling_auto_focus) {
            shiftFocus(focusStrCmd);
        }
        numFocusPos++;
        hasGoneForward = 1;

        // for kst display?
        memcpy(output_buffer, unpacked_image, CAMERA_NUM_PX * sizeof(uint16_t));
        // pass off the image bytes for sending to clients
        memcpy(camera_raw, output_buffer, CAMERA_NUM_PX * sizeof(uint16_t));
    }

    if (restoreBinningFactor() < 0) {
        exit(EXIT_FAILURE);
    }

    if (verbose) {
        printf("Autofocus concluded with %d tries remaining.\n", remainingFocusPos);
    }

    // Move to optimal focus pos
    // Do bounds checking on resultant pos
    if (bestFocusPos > all_camera_params->max_focus_pos) {
        printf("Auto focus is greater than max possible focus, "
                "so just use that.\n");
        bestFocusPos = all_camera_params->max_focus_pos;
    // this outcome is highly unlikely but just in case
    } else if (bestFocusPos < all_camera_params->min_focus_pos) {
        printf("Auto focus is less than min possible focus, "
                "so just use that.\n");
        bestFocusPos = all_camera_params->min_focus_pos;
    }
    // Due to backlash, return to the optimal focus position via the direction
    // we measured it: move to beginning
    if (beginAutoFocus() < 1) {
        printf("Error moving back to beginning of auto-focusing range. Skipping to taking "
                "observing images...\n");

        // return to default focus position
        if (defaultFocusPosition() < 1) {
            printf("Error moving to default focus position.\n");
            all_camera_params->focus_mode = 0;
            return -1;
        }

        // abort auto-focusing process
        all_camera_params->focus_mode = 0;
        return -1;
    }
    // and THEN move to the optimal pos.
    sprintf(focusStrCmd, "mf %i\r", 
        bestFocusPos - all_camera_params->focus_position);
    shiftFocus(focusStrCmd);

    // Clean up
    fclose(af_file);
    af_file = NULL;

    // Necessary to avoid going into legacy autofocus mode if we drop out
    // due to too many AF attempts.
    all_camera_params->focus_mode = 0;
    return 0;
}


/* Function to take observing images and solve for pointing using Astrometry.
** Main function for the Astrometry thread in commands.c.
** Input: None.
** Output: A flag indicating successful round of image + solution by the camera 
** (e.g. if the camera can't open the observing file, the function will 
** automatically return with -1).
*/
int doCameraAndAstrometry(void)
{
    // these must be static since this function is called perpetually in 
    // updateAstrometry thread
    static double * star_x = NULL, * star_y = NULL, * star_mags = NULL;
    static uint16_t * output_buffer = NULL;
    static int first_time = 1, af_photo = 0;
    static FILE * af_file = NULL;
    static FILE * fptr = NULL;
    static int num_focus_pos;
    static int * blob_mags;
    int blob_count;
    struct timeval tv;
    double photo_time;
    char datafile[100], buff[100], date[256];
    // static: af_filename only defined on first autofocus pass, but in
    // subsequent calls to doCameraAndAstrometry() gets passed to
    // calculateOptimalFocus()
    static char af_filename[256];
    char filename[256] = "";
    struct timespec camera_tp_beginning, camera_tp_end; 
    time_t seconds = time(NULL);
    struct tm * tm_info;
  
    // uncomment line below for testing the values of each field in the global 
    // structure for blob_params
    if (verbose) {
        verifyBlobParams();
    }

    tm_info = gmtime(&seconds);
    all_astro_params.rawtime = seconds;
    // if it is a leap year, adjust tm_info accordingly before it is passed to 
    // calculations in lostInSpace
    if (isLeapYear(tm_info->tm_year)) {
        // if we are on Feb 29
        if (tm_info->tm_yday == 59) {  
            // 366 days in a leap year      
            tm_info->tm_yday++;  
            // we are still in February 59 days after January 1st (Feb 29)            
            tm_info->tm_mon -= 1;
            tm_info->tm_mday = 29;
        } else if (tm_info->tm_yday > 59) {
            tm_info->tm_yday++;           
        }
    }

    // data file to pass to lostInSpace
    strftime(datafile, sizeof(datafile), 
             "/home/starcam/Desktop/TIMSC/data_%b-%d.txt", tm_info);
    
    // set file descriptor for observing file to NULL in case of previous bad
    // shutdown or termination of Astrometry
    if (fptr != NULL) {
        fclose(fptr);
        fptr = NULL;
    }
    
    if ((fptr = fopen(datafile, "a")) == NULL) {
        fprintf(stderr, "Could not open obs. file %s: %s.\n", datafile,
            strerror(errno));
        return -1;
    }

    if (first_time) {
        solveState = INIT;
        output_buffer = calloc(CAMERA_NUM_PX, sizeof(uint16_t));
        if (output_buffer == NULL) {
            fprintf(stderr, "Error allocating output buffer: %s.\n", 
                    strerror(errno));
            return -1;
        }

        blob_mags = calloc(default_focus_photos, sizeof(int));
        if (blob_mags == NULL) {
            fprintf(stderr, "Error allocating array for blob mags: %s.\n", 
                    strerror(errno));
            return -1;
        }

        // NOTE(evanmayer): After switching to the iDS peak API, this logging
        // will be deprecated. We'll save this info in the FITS files instead.
        strftime(buff, sizeof(buff), "%B %d Observing Session - beginning "
                                     "%H:%M:%S GMT", tm_info);
        fprintf(fptr, "\n");
        fprintf(fptr, "# ********************* %s *********************\n", buff);
        fprintf(fptr, "# Camera model: %s\n", default_metadata.detector);
        fprintf(fptr, "# ----------------------------------------------------\n");
        fprintf(fptr, "# Exposure: %f milliseconds\n", default_metadata.exptime * 1000.0);
        fprintf(fptr, "# Pixel clock: %f MHz\n", default_metadata.pixelclk);
        fprintf(fptr, "# Frame rate achieved (desired is 10): %f\n", default_metadata.framerte);
        fprintf(fptr, "# Trigger delay (microseconds): %f\n", default_metadata.trigdlay);
        fprintf(fptr, "# Auto shutter: %i\n", default_metadata.autoexp);
        fprintf(fptr, "# ----------------------------------------------------\n");
        fprintf(fptr, "# Sensor ID/type: %lu\n", default_metadata.sensorid);
        fprintf(fptr, "# Sensor bit depth %u\n", default_metadata.bitdepth);
        fprintf(fptr, "# Maximum image width and height: %i, %i\n", 
                       CAMERA_WIDTH, CAMERA_HEIGHT);
        fprintf(fptr, "# Pixel size (micrometers): %.2f\n", 
                      default_metadata.pixsize1);
        fprintf(fptr, "# Mono gain setting: %.2fx base\n", default_metadata.gainfact);
        fprintf(fptr, "# Auto gain (should be disabled): %i\n", (int) default_metadata.autogain);
        fprintf(fptr, "# Auto exposure (should be disabled): %i\n", (int) default_metadata.autoexp);
        fprintf(fptr, "# Auto black level (should be disabled): %i\n", (int) default_metadata.autoblk);
        fprintf(fptr, "# Black level offset (desired is 50): %u\n", default_metadata.bloffset);

        // write header to data file
        if (fprintf(fptr, "C time,GMT,Blob #,RA (deg),DEC (deg),RA_OBS (deg),DEC_OBS (deg),FR (deg),PS,"
                          "ALT (deg),AZ (deg),IR (deg),Astrom. solve time "
                          "(msec),Solution Uncertainty (arcsec),Camera time (msec)\n") < 0) {
            fprintf(stderr, "Error writing header to observing file: %s.\n", 
                    strerror(errno));
        }

        fflush(fptr);
        first_time = 0;
    }

    // if we are at the start of auto-focusing (either when camera first runs or 
    // user re-enters auto-focusing mode)
    #ifdef AF_ALGORITHM_NEW
    // Hijack AF with new alg, which should set
    // all_camera_params.focus_mode = 0 when done to bypass old AF
    if (all_camera_params.begin_auto_focus && all_camera_params.focus_mode) {
        solveState = AUTOFOCUS;
        doContrastDetectAutoFocus(&all_camera_params, tm_info, output_buffer);
        all_camera_params.focus_mode = 0;
    }
    #endif
    if (all_camera_params.begin_auto_focus && all_camera_params.focus_mode) {
        num_focus_pos = 0;
        send_data = 0;

        // check that our blob magnitude array is big enough for number of
        // photos we take per auto-focusing position
        if (all_camera_params.photos_per_focus != default_focus_photos) {
            printf("Reallocating blob_mags array to allow for different # of "
                   "auto-focusing pictures.\n");
            default_focus_photos = all_camera_params.photos_per_focus;
            blob_mags = realloc(blob_mags, sizeof(int) * default_focus_photos);
        }

        // check that end focus position is at least 25 less than max focus
        // position
        if (all_camera_params.max_focus_pos - all_camera_params.end_focus_pos 
            < 25) {
            printf("Adjusting end focus position to be 25 less than max focus "
                   "position.");
            all_camera_params.end_focus_pos = all_camera_params.max_focus_pos 
                                              - 25;
        }

        // check that beginning focus position is at least 25 above min focus
        // position
        if (all_camera_params.start_focus_pos - all_camera_params.min_focus_pos
            < 25) {
            printf("Adjusting beginning focus position to be 25 more than min "
                   "focus position.");
            all_camera_params.start_focus_pos = all_camera_params.min_focus_pos
                                                + 25;
        }

        // get to beginning of auto-focusing range
        if (beginAutoFocus() < 1) {
            printf("Error beginning auto-focusing process. Skipping to taking "
                   "observing images...\n");

            // return to default focus position
            if (defaultFocusPosition() < 1) {
                printf("Error moving to default focus position.\n");
                closeCamera();
                return -1;
            }

            // abort auto-focusing process
            all_camera_params.focus_mode = 0;
        }
        
        usleep(1000000); 

        if (af_file != NULL) {
            fclose(af_file);
            af_file = NULL;
        }

        // clear previous contents of auto-focusing file (open in write mode)
        strftime(
            af_filename,
            sizeof(af_filename),
            "/home/starcam/Desktop/TIMSC/auto_focus_starting_%Y-%m-%d_%H-%M-%S.txt",
            tm_info
        );
        if (verbose) {
            printf("Opening auto-focusing text file: %s\n", af_filename);
        }

        if ((af_file = fopen(af_filename, "w")) == NULL) {
            fprintf(stderr, "Could not open auto-focusing file: %s.\n", 
                    strerror(errno));
            return -1;
        }

        all_camera_params.begin_auto_focus = 0;

        // turn dynamic hot pixels off to avoid removing blobs during focusing
        prev_dynamic_hp = all_blob_params.dynamic_hot_pixels;
        if (verbose) {
            printf("Turning dynamic hot pixel finder off for auto-focusing.\n");
        }
        all_blob_params.dynamic_hot_pixels = 0;
        
        // link the auto-focusing txt file to Kst for plotting
        unlink("/home/starcam/Desktop/TIMSC/latest_auto_focus_data.txt");
        symlink(af_filename, 
                "/home/starcam/Desktop/TIMSC/latest_auto_focus_data.txt");
    }

    #ifdef IDS_PEAK
    // If the user has triggered a new hot pixel mask, or want to be using 
    // dynamic hot pixel masking, re-make the mask internal hot pixel list
    // before capture.
    if (all_blob_params.make_static_hp_mask || all_blob_params.dynamic_hot_pixels) {
        if (renewCameraHotPixels() < 0) {
            fprintf(stderr, "Could not re-make internal hot pixel mask.\n");
        }
    }
    #endif

    solveState = IMAGE_CAP;
    taking_image = 1;
    // Ian Lowe, 1/9/24, adding new logic to look for a trigger from a FC or sleep instead
    if (all_trigger_params.trigger_mode == 1) {
        while (all_trigger_params.trigger == 0 && shutting_down != 1)
        {
            usleep(all_trigger_params.trigger_timeout_us); // default sleep 100µs while we wait for triggers
        }
        all_trigger_params.trigger = 0; // set the trigger to 0 now that we are going to take an image
        if (imageCapture() < 0) {
            fprintf(stderr, "Could not complete image capture: %s.\n", 
                strerror(errno));
        }
    } else {
        if (imageCapture() < 0) {
            fprintf(stderr, "Could not complete image capture: %s.\n", 
                strerror(errno));
        }
    }
    taking_image = 0;

    gettimeofday(&tv, NULL);
    photo_time = tv.tv_sec + ((double) tv.tv_usec)/1000000.;
    all_astro_params.photo_time = photo_time;

    #ifndef IDS_PEAK
    if (imageTransfer(unpacked_image) < 0) {
        fprintf(stderr, "Could not complete image transfer: %s.\n", 
           strerror(errno));
        return -1;
    }
    #else
    if (imageTransfer(unpacked_image) < 0) {
        fprintf(stderr, "Could not complete image transfer: %s.\n", 
           strerror(errno));
        return -1;
    }
    #endif


    // TODO(evanmayer): implement a FITS image loader
    // testing pictures that have already been taken
    // if you uncomment this for testing, you may need to change the path.
    // if (loadDummyPicture(L"/home/starcam/saved_image_2022-07-06_08-31-30.bmp", //L"/home/starcam/Desktop/TIMSC/img/load_image.bmp", 
    //                      (char **) &memory) == 1) {
    //     if (verbose) {
    //         printf("Successfully loaded test picture.\n");
    //     }
    // } else {
    //     fprintf(stderr, "Error loading test picture: %s.\n", strerror(errno));
    //     // can't solve without a picture to solve on!
    //     usleep(1000000);
    //     return -1;
    // }

    // find the blobs in the image
    blob_count = findBlobs(unpacked_image, CAMERA_WIDTH, CAMERA_HEIGHT, &star_x, 
                           &star_y, &star_mags, output_buffer);
    // Add some logic to automatically try filtering the image
    // if the number of blobs found is not in some nice passband
    
    if (blob_count < MIN_BLOBS || blob_count > MAX_BLOBS)
    {
        printf("Couldn't find an appropriate number of blobs, filtering image...\n");
        all_blob_params.high_pass_filter = 1;
        blob_count = findBlobs(unpacked_image, CAMERA_WIDTH, CAMERA_HEIGHT, &star_x, 
                           &star_y, &star_mags, output_buffer);
        all_blob_params.high_pass_filter = 0;
    }
    // New 3x3 flux weighted centroiding algorithm from 2023 added below
    // temp variable to store the position of each blob in the flattened array
    int image_locs[9];
    // arrays of positions to centroid with
    double x_locs[9];
    double y_locs[9];
    // nowq we loop over blobcount to grab positions 
    for (int i = 0; i < blob_count; i++)
    {   
        // printf("made it in on iteration %d of %d\n", i+1, blob_count);
        double sum = 0; // variable to store total flux
        double new_x = 0; // new centroided locations 
        double new_y = 0;
        // grab the locations on the unfiltered map
        image_locs[0] = (int) ((star_x[i]-1)+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]+1));
        image_locs[1] = (int) ((star_x[i]  )+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]+1));
        image_locs[2] = (int) ((star_x[i]+1)+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]+1));
        image_locs[3] = (int) ((star_x[i]-1)+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]  ));
        image_locs[4] = (int) ((star_x[i]  )+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]  ));
        image_locs[5] = (int) ((star_x[i]+1)+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]  ));
        image_locs[6] = (int) ((star_x[i]-1)+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]-1));
        image_locs[7] = (int) ((star_x[i]  )+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]-1));
        image_locs[8] = (int) ((star_x[i]+1)+CAMERA_WIDTH*(CAMERA_HEIGHT-star_y[i]-1));
        for (int j = 0; j < 9; j++)
        {
            sum += (double) unpacked_image[image_locs[j]]; // get the total flux in the 3x3
        }
        // grab the actual x + y positions in 2d instead of flattened
        x_locs[0] = x_locs[3] = x_locs[6] = star_x[i]-1;
        x_locs[1] = x_locs[4] = x_locs[7] = star_x[i]  ;
        x_locs[2] = x_locs[5] = x_locs[8] = star_x[i]+1;
        y_locs[0] = y_locs[1] = y_locs[2] = star_y[i]+1;
        y_locs[3] = y_locs[4] = y_locs[5] = star_y[i]  ;
        y_locs[6] = y_locs[7] = y_locs[8] = star_y[i]-1;
        // flux weight the locations
        for (int j2 = 0; j2 < 9; j2++)
        {
            x_locs[j2] = x_locs[j2]*unpacked_image[image_locs[j2]]/sum;
            y_locs[j2] = y_locs[j2]*unpacked_image[image_locs[j2]]/sum;
        }
        for (int j3 = 0; j3 < 9; j3++)
        {
            new_x += x_locs[j3];
            new_y += y_locs[j3];
        }
        star_x[i] = new_x;
        star_y[i] = new_y;
    }

    // make kst display the filtered image 
    memcpy(output_buffer, unpacked_image, CAMERA_NUM_PX * sizeof(uint16_t));

    // pass off the image bytes for sending to clients
    memcpy(camera_raw, output_buffer, CAMERA_NUM_PX * sizeof(uint16_t));

    // get current time right after exposure
    if (clock_gettime(CLOCK_REALTIME, &camera_tp_beginning) == -1) {
        fprintf(stderr, "Error starting camera timer: %s.\n", strerror(errno));
    }

    // now have to distinguish between auto-focusing actions and solving
    // ECM N.B.: If AF_ALGORITHM_NEW defined, you'll never go in here
    if (all_camera_params.focus_mode && !all_camera_params.begin_auto_focus) {
        solveState = AUTOFOCUS;
        int brightest_blob, max_flux, focus_step;
        int brightest_blob_x, brightest_blob_y = 0;
        char focus_str_cmd[10];
        char time_str[100];

        if (verbose) {
            printf("\n>> Still auto-focusing!\n");
        }

        // find the brightest blob per picture
        brightest_blob = -1;
        for (int blob = 0; blob < blob_count; blob++) {
            if (star_mags[blob] > brightest_blob) {
                brightest_blob = star_mags[blob];
                brightest_blob_x = (int) star_x[blob];
                brightest_blob_y = (int) star_y[blob];
            }
        }
        blob_mags[af_photo++] = brightest_blob;
        printf("Brightest blob for photo %d at focus %d has value %d.\n", 
               af_photo, all_camera_params.focus_position, brightest_blob);

        strftime(time_str, sizeof(time_str), "%Y-%m-%d_%H-%M-%S", tm_info);
        sprintf(date, "/home/starcam/Desktop/TIMSC/img/auto_focus_at_%d_"
                      "brightest_blob_%d_at_x%d_y%d_%s.png", 
                all_camera_params.focus_position, brightest_blob, 
                brightest_blob_x, brightest_blob_y, time_str);
        if (verbose) {
            printf("Saving auto-focusing image as: %s\n", date);
        }
        snprintf(filename, 256, "%s", date);

        if (af_photo >= all_camera_params.photos_per_focus) {
            if (verbose) {
                printf("> Processing auto-focus images for focus %d -> do not "
                       "take an image...\n", all_camera_params.focus_position);
            }

            // find brightest of three brightest blobs for this batch of images
            max_flux = -1;
            for (int i = 0; i < all_camera_params.photos_per_focus; i++) {
                if (blob_mags[i] > max_flux) {
                    max_flux = blob_mags[i];
                }
            }
           
            all_camera_params.flux = max_flux;
            printf("(*) Brightest blob among %d photos for focus %d is %d.\n", 
                   all_camera_params.photos_per_focus, 
                   all_camera_params.focus_position,
                   max_flux);

            fprintf(af_file, "%3d\t%5d\n", max_flux,
                    all_camera_params.focus_position);
            fflush(af_file);

            send_data = 1;

            // if clients are listening and we want to guarantee data is sent to
            // them before continuing with auto-focusing, wait until data_sent
            // confirmation. If there are no clients, no need to slow down auto-
            // focusing
            if (num_clients > 0) {
                while (!telemetry_sent) {
                    if (verbose) {
                        printf("> Waiting for data to send to client...\n");
                    }

                    usleep(100000);
                }
            }

            send_data = 0;

            // since we are moving to next focus, re-start photo counter and get
            // rid of previous blob magnitudes
            af_photo = 0;
            for (int i = 0; i < all_camera_params.photos_per_focus; i++) {
                blob_mags[i] = 0;
            }

            // We have moved to the end (or past) the end focus position, so
            // calculate best focus and exit
            if (all_camera_params.focus_position >= 
                all_camera_params.end_focus_pos) {
                int best_focus; 
                all_camera_params.focus_mode = 0;
                // at very last focus position
                num_focus_pos++;
                
                best_focus = calculateOptimalFocus(num_focus_pos, af_filename);
                if (best_focus == -1000) {
                    // if we can't find optimal focus from auto-focusing data, 
                    // just go to the default
                    defaultFocusPosition();
                } else {
                    // if the calculated auto focus position is outside the
                    // possible range, set it to corresponding nearest focus
                    if (best_focus > all_camera_params.max_focus_pos) {
                        printf("Auto focus is greater than max possible focus, "
                               "so just use that.\n");
                        best_focus = all_camera_params.max_focus_pos;
                    } else if (best_focus < all_camera_params.min_focus_pos) {
                        printf("Auto focus is less than min possible focus, "
                               "so just use that.\n");
                        // this outcome is highly unlikely but just in case
                        best_focus = all_camera_params.min_focus_pos;
                    }

                    sprintf(focus_str_cmd, "mf %i\r", 
                            best_focus - all_camera_params.focus_position);
                    shiftFocus(focus_str_cmd);
                }

                fclose(af_file);
                af_file = NULL;

                // turn dynamic hot pixels back to whatever user had specified
                if (verbose) {
                    printf("> Auto-focusing finished, so restoring dynamic hot "
                           "pixels to previous value...\n");
                }

                all_blob_params.dynamic_hot_pixels = prev_dynamic_hp;
                if (verbose) {
                    printf("Now all_blob_params.dynamic_hot_pixels = %d\n", 
                           all_blob_params.dynamic_hot_pixels);
                }
            } else {
                // Move to the next focus position if we still have positions to
                // cover
                focus_step = min(all_camera_params.focus_step, 
                             all_camera_params.end_focus_pos - 
                             all_camera_params.focus_position);
                sprintf(focus_str_cmd, "mf %i\r", focus_step);
                if (!cancelling_auto_focus) {
                    shiftFocus(focus_str_cmd);
                    usleep(100000);
                }
                num_focus_pos++;
            }
        }
    } else {
        double start, end, camera_time;
        send_data = 1;

        if (verbose) {
            printf(">> No longer auto-focusing!\n");
        }

        strftime(date, sizeof(date), "/home/starcam/Desktop/TIMSC/img/"
                                     "saved_image_%Y-%m-%d_%H-%M-%S.png", 
                                     tm_info);
        snprintf(filename, 256, "%s", date);

        // write blob and time information to data file
        strftime(buff, sizeof(buff), "%b %d %H:%M:%S", tm_info); 
        printf("\nTime going into Astrometry.net: %s\n", buff);

        if (fprintf(fptr, "%li,%s,", seconds, buff) < 0) {
            fprintf(stderr, "Unable to write time and blob count to observing "
                            "file: %s.\n", strerror(errno));
        }
        fflush(fptr);

        // solve astrometry
        if (verbose) {
            printf("\n> Trying to solve astrometry...\n");
        }

        solveState = ASTROMETRY;
        if (lostInSpace(star_x, star_y, star_mags, blob_count, tm_info, 
                        datafile) != 1) {
            printf("\n(*) Could not solve Astrometry.\n");
        } else {
            // let the astro thread know to send data
            image_solved[0] = 1;
            image_solved[1] = 1;
        }

        // get current time right after solving
        if (clock_gettime(CLOCK_REALTIME, &camera_tp_end) == -1) {
            fprintf(stderr, "Error ending timer: %s.\n", strerror(errno));
        }

        // calculate time it took camera program to run in nanoseconds
        start = (double) (camera_tp_beginning.tv_sec*1e9) + 
                (double) camera_tp_beginning.tv_nsec;
        end = (double) (camera_tp_end.tv_sec*1e9) + 
              (double) camera_tp_end.tv_nsec;
        camera_time = end - start;
        printf("(*) Camera completed one round in %f msec.\n", 
               camera_time*1e-6);

        // write this time to the data file
        if (fprintf(fptr, ",%f\n", camera_time*1e-6) < 0) {
            fprintf(stderr, "Unable to write Astrometry solution time to "
                            "observing file: %s.\n", strerror(errno));
        }
        fflush(fptr);
        fclose(fptr);
        fptr = NULL;
    }

    #ifndef IDS_PEAK
    saveImageToDisk(filename);
    #else
    saveFITStoDisk(unpacked_image);
    #endif

    // printf("Saving captured frame to \"%s\"\n", filename);
    // unlink whatever the latest saved image was linked to before
    // unlink("/home/starcam/Desktop/TIMSC/imh/latest_saved_image.png");
    // sym link current date to latest image for live Kst updates
    // symlink(date, "/home/starcam/Desktop/TIMSC/img/latest_saved_image.png");

    // make a table of blobs for Kst
    if (makeTable("makeTable.txt", star_mags, star_x, star_y, blob_count) != 1) {
        printf("Error (above) writing blob table for Kst.\n");
    }

    // free alloc'd variables when we are shutting down
    if (shutting_down) {
        if (verbose) {
            printf("\n> Freeing allocated variables in camera.c...\n");
        }

        if (output_buffer != NULL) {
            free(output_buffer);
        }
        
        if (mask != NULL) {
            free(mask);
        }
        
        if (blob_mags != NULL) {
            free(blob_mags);
        }

        if (star_x != NULL) {
            free(star_x);
        }
        
        if (star_y != NULL) {
            free(star_y);
        }
        
        if (star_mags != NULL) {
            free(star_mags);
        }
    }
    return 1;
}
