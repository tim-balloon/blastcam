#ifndef CAMERA_H
#define CAMERA_H
#include "astrometry.h"

// SO Star Camera is 1936 width by 1216 height; BLAST is 1392 by 1040
#define CAMERA_WIDTH   1936 	 // [px]
#define CAMERA_HEIGHT  1216	     // [px]
#define CAMERA_MARGIN  0		 // [px]
#define CAMERA_MAX_PIXVAL 4095 //  2**12
// pixel scale search range bounds -> 6.0 to 6.5 for SO, 6.0 to 7.0 for BLAST
#define MIN_PS         6.0       // [arcsec/px]
#define MAX_PS         7.0		 // [arcsec/px]
#define STATIC_HP_MASK "/home/starcam/Desktop/TIMSC/static_hp_mask.txt"
#define dut1           -0.23

extern HIDS camera_handle;
extern int shutting_down;
extern int send_data;
extern int taking_image;
extern int image_solved[2];
extern struct mcp_astrometry mcp_astro;

/* triggering parameters */
struct trigger_params
{
    int trigger_mode; // 0 is auto triggered with a sleep, 1 waits for a trigger
    int trigger; // 1 to take image if trigger mode is 1
    int trigger_timeout_us; // time in µs to wait between checks for a trigger
};


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
int loadCamera();
int initCamera();
int doContrastDetectAutoFocus(struct camera_params* all_camera_params, struct tm* tm_info, char* output_buffer);
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

#endif