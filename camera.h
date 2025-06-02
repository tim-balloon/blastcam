#ifndef CAMERA_H
#define CAMERA_H
#include "astrometry.h"

#ifdef IDS_PEAK
#include <ids_peak_comfort_c/ids_peak_comfort_c.h>
extern peak_camera_handle hCam;
#else
#include <ueye.h>
extern HIDS camera_handle;
#endif

// SO Star Camera is 1936 width by 1216 height; BLAST is 1392 by 1040
#ifndef IDS_PEAK
#define CAMERA_WIDTH   1936 // [px]
#define CAMERA_HEIGHT  1216 // [px]
// pixel scale search range bounds -> 6.0 to 6.5 for SO, 6.0 to 7.0 for BLAST,
#define MIN_PS         6.0  // [arcsec/px]
#define MAX_PS         7.0  // [arcsec/px]
#else
// TIMSC is IMX542
// Datasheet says array is 5328 x 3040, but this includes overscan. The number
// recommended recording pixels is an area is 8px smaller each side.
#define CAMERA_WIDTH   5320 // [px]
#define CAMERA_HEIGHT  3032 // [px]
// ~6.63 for TIMSC with 85mm lens
#define MIN_PS         6.0  // [arcsec/px]
#define MAX_PS         7.0  // [arcsec/px]
#endif
#define CAMERA_MARGIN  0		 // [px]
#define CAMERA_MAX_PIXVAL 4095 //  2**12
#define STATIC_HP_MASK "/home/starcam/Desktop/TIMSC/static_hp_mask.txt"
#define dut1           -0.23

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


// There is no equivalent of iDS Software Suite sensorInfo, but it's useful for 
// collecting info from multiple other structs, so re-implement some parts here.
// A little preview: eventually sensor/capture data will be saved in headers of
// FITS files.
struct fits_metadata_t {

    // Capture data

    char origin[10]; // ORIGIN: program used to generate file
    char instrume[10]; // INSTRUME: instrument name
    char telescop[30]; // TELESCOP: lens name
    char observat[10]; // OBSERVAT: observatory name
    char observer[10]; // OBSERVER: observer name
    char filename[200]; // FILENAME: basename + ext on disk
    char date[200]; // DATE: time of file creation (UTC)
    char utc_obs[200]; // UTC-OBS: time of observation start (UTC)
    float julian; // JULIAN: Julian date of obs start
    char filter[20]; // FILTER: fileter name
    float ccdtemp; // CCDTEMP: camera temp (C)
    int16_t focus; // FOCUS: focus position (encoder units)
    int8_t aperture; // APERTURE: aperture position (10x fstop)
    float exptime; // EXPTIME: total exposure time (s)
    char bunit[4]; // BUNIT: physical unit of array values (ADU)

    // Compression settings, not all are used for integer images
    // https://heasarc.gsfc.nasa.gov/docs/software/fitsio/c/c_user/node41.html

    char fzalgor[12]; // FZALGOR  - 'RICE_1' , 'GZIP_1', 'GZIP_2', 'HCOMPRESS_1', 'PLIO_1', 'NONE'
    char fztile[4]; // FZTILE   - 'ROW', 'WHOLE', or '(n,m)'

    // Sensor settings

    char detector[64]; // DETECTOR: sensor name
    uint8_t bitdepth; // BITDEPTH: requested bit depth of camera, not equivalent to BITPIX
    float pixscal1; // PIXSCAL1: plate scale, axis 1 (arcsec/px)
    float pixscal2; // PIXSCAL2: plate scale, axis 2 (arcsec/px)
    float pixsize1; // PIXSIZE1: pixel pitch, axis 1 (micron)
    float pixsize2; // PIXSIZE2: pixel pitch, axis 2 (micron)
    float darkcur; // DARKCUR: avg dark current (e-/px/s)
    float rdnoise1; // RDNOISE1: read noise (e-)
    uint8_t ccdbin1; // CCDBIN1: x-axis binning factor
    uint8_t ccdbin2; // CCDBIN2: y-axis binning factor
    uint8_t pixelclk; // PIXELCLK: pixel clock (MHz)
    float framerte; // FRAMERTE: framerate (Hz)
    float gainfact; // GAINFACT: iDS gain factor setting (e.g. 2.0x)
    // GAIN1: sensor gain, e-/DN...depends on GAINFACT, not known a-priori unless calibrated
    float trigdlay; // TRIGDLAY: trigger delay (ms)
    uint16_t bloffset; // BLOFFSET: black level offset setting, arb units
    int16_t autogain; // AUTOGAIN: automatic gain control on (1) off (0)
    int16_t autoshut; // AUTOSHUT: automatic shutter control on (1) off (0)
    int16_t autofrte; // AUTOFRTE: automatic framerate control on (1) off (0)
    int16_t autoblk; // AUTOBLK: automatic black level offset on (1) off (0)

    // TODO(evanmayer): add more WCS info fields?
    // Pointing data (to be added on plate solve?)

    // SITEELEV: altitude, m
    // SITELAT: latitude, (DD:MM:SS.S N)
    // SITELAT: latitude, (DD:MM:SS.S W)
    // AIRMASS:
    // AZIMUTH:
    // ELEVAT:
    // RA:
    // DEC:
    // ROTANGLE: image rotation angle
    // EQUINOX: (epoch of RA,DEC)
};


extern struct blob_params all_blob_params;
extern struct trigger_params all_trigger_params;

int setCameraParams();
void setSaveImage();
int loadCamera();
int initCamera();
int getNumberOfCameras(int* pNumCams);
int setExposureTime(double exposureTimeMs);
double getFps(void);
int imageCapture(void);
#ifndef IDS_PEAK
int imageTransfer(uint16_t* pUnpackedImage);
int saveImageToDisk(char* filename);
#else
int imageTransfer(uint16_t* pUnpackedImage, char* filename);
int saveImageToDisk(char* filename, peak_frame_handle hFrame);
#endif
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