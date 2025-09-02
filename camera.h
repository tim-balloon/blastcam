#ifndef CAMERA_H
#define CAMERA_H
#include "astrometry.h"

#include <ids_peak_comfort_c/ids_peak_comfort_c.h>
extern peak_camera_handle hCam;


// TIMSC is IMX542
// Datasheet says array is 5328 x 3040, but this includes overscan. The number
// recommended recording pixels is an area is 8px smaller each side.
#define CAMERA_WIDTH   5320 // [px]
#define CAMERA_HEIGHT  3032 // [px]
// ~6.63 for TIMSC with 85mm lens
#define MIN_PS         6.0  // [arcsec/px]
#define MAX_PS         7.0  // [arcsec/px]

#define CAMERA_NUM_PX (CAMERA_WIDTH * CAMERA_HEIGHT)
// higher than 2 requires FPGA binning. I noticed no speed gains from on-sensor
// vs. FPGA, or even vs. on host. FPGA binning default.
#define CAMERA_FOCUS_BINFACTOR 4
#define CAMERA_MARGIN 0 // [px]
#define CAMERA_MAX_PIXVAL 4095 //  2**12
#define MIN_BLOBS 4
#define MAX_BLOBS 300
#define STATIC_HP_MASK "/home/starcam/Desktop/TIMSC/static_hp_mask.txt"
#define dut1           -0.23

extern int shutting_down;
extern int send_data;
extern int taking_image;
extern int image_solved[2];

/* triggering parameters */
struct trigger_params
{
    int trigger_mode; // 0 is auto triggered with a sleep, 1 waits for a trigger
    int trigger; // 1 to take image if trigger mode is 1
    int trigger_timeout_us; // time in µs to wait between checks for a trigger
};

enum solveState_t
{
    UNINIT,
    INIT,
    IMAGE_CAP,
    IMAGE_XFER,
    HOTPIX_MASK,
    FILTERING,
    BLOB_FIND,
    AUTOFOCUS,
    ASTROMETRY,
    NUM_STATES
};
extern enum solveState_t solveState;


/* Blob-finding parameters */
#pragma pack(push, 1)
struct blob_params {
    int spike_limit;            // where dynamic hot pixel will designate as hp
    int dynamic_hot_pixels;     // (bool) search for dynamic hot pixels
    int r_smooth;               // image smooth filter radius [px]
    int high_pass_filter;       // 0 == off, 1 == on
    int r_high_pass_filter;     // image high pass filter radius [px]
    int centroid_search_border; // px dist from image edge to start star search
    int filter_return_image;    // 1 == true; 0 = false
    float n_sigma;              // pixels > this*noise + mean = blobs
    int unique_star_spacing;    // min. pixel spacing between stars [px]
    int make_static_hp_mask;    // re-make static hp map with current image
    int use_static_hp_mask;     // flag to use the current static hot pixel map
};
#pragma pack(pop)

extern struct blob_params all_blob_params;
extern struct trigger_params all_trigger_params;
extern struct camera_params all_camera_params;

int setCameraParams();
void setSaveImage();
int initMessageQueue(void);
int pollMessageQueue(void);
int closeMessageQueue(void);
int loadCamera();
int initCamera();
int getNumberOfCameras(int* pNumCams);
int setExposureTime(double exposureTimeMs);

int imageCapture(void);
int getFps(double* pCurrentFps);
int imageTransfer(uint16_t* pUnpackedImage);
int saveImageToDisk(char* filename, peak_frame_handle hFrame);
int setMonoAnalogGain(double analogGain);
int doCameraAndAstrometry();
void clean();
void closeCamera();
const char * printCameraError();
int isLeapYear(int year);
void verifyBlobParams();
int makeTable(char * filename, double * star_mags, double * star_x, 
              double * star_y, int blob_count);
int findBlobs(uint16_t * input_buffer, int w, int h, double ** star_x, 
              double ** star_y, double ** star_mags, uint16_t * output_buffer);
void unpack_mono12(uint16_t * packed, uint16_t * unpacked, int num_pixels);

void boxcarFilterImage(uint16_t * ib, int i0, int j0, int i1, int j1, int r_f, 
                       double * filtered_image);

#endif
