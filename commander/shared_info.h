/**
 * @file shared_info.h
 * @author Ian Lowe
 * @brief shared info for the 
 * @version 0.1
 * @date 2025-08-20
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef _SHARED_INFO_H
#define _SHARED_INFO_H

 #define IP_ADDR "127.0.0.1"
 #define ENCODER_PORT "4970"
 #define COMMAND_PORT "4950"

 struct star_cam_return {
    double logOdds; // significance of point sources
    double latitude; // payload lat
    double longitude; // payload long
    double heightWGS84; // payload alt above reference surface
    double exposureTime; // milliseconds
    double gainFact; // x times base gain factor in DN/e-
    double solveTimeLimit; // time allowed to solve an image
    float focusPos; // desired focus position, encoder units
    int minFocusPos;
    int maxFocusPos;
    int focusMode; // autofocus or manual?
    int startPos; // start of autofocus range
    int endPos; // end of autofocus range
    int focusStep; // step size in encoder units
    int photosPerStep; // photos per AF step
    int setFocusInf; // move focus position to infinity
    int apertureSteps; // number of positions +/- to move the aperture
    int maxAperture; // open aperture fully
    float aperture;
    int makeHP; // set to 20 to do it, makes a new static hot pixel map
    int useHP; // use the hot pixel map to mask bad pixels
    float blobParams[9]; // blobfinding parameters...
    int trigger_mode; // 0 for auto, 1 for software triggered
    int trigger_timeout_us; // timeout between trigger checks in µs default 100
};

struct star_cam_capture {
    int fc;
    char target[16];
    int inCharge; // make sure an incharge FC sent this just in case
    double logOdds; // significance of point sources
    int update_logOdds; // is this a new commanded value?
    double latitude; // payload lat
    int update_lat; // is this a new commanded value?
    double longitude; // payload long
    int update_lon; // is this a new commanded value?
    double heightWGS84; // payload alt above reference surface
    int update_height; // is this a new commanded value?
    double exposureTime; // milliseconds
    int update_exposureTime; // is this a new commanded value?
    double gainFact; // x times base gain in DN/e-
    int update_gainFact;
    double solveTimeLimit; // time allowed to solve an image
    int update_solveTimeLimit; // is this a new commanded value?
    float focusPos; // desired focus position, encoder units
    int update_focusPos; // is this a new commanded value?
    int focusMode; // autofocus or manual?
    int update_focusMode; // is this a new commanded value?
    int startPos; // start of autofocus range
    int update_startPos; // is this a new commanded value?
    int endPos; // end of autofocus range
    int update_endPos; // is this a new commanded value?
    int focusStep; // step size in encoder units
    int update_focusStep; // is this a new commanded value?
    int photosPerStep; // ...
    int update_photosPerStep; // is this a new commanded value?
    int setFocusInf; // move focus position to infinity
    int update_setFocusInf; // is this a new commanded value?
    int apertureSteps; // number of positions +/- to move the aperture
    int update_apertureSteps; // is this a new commanded value?
    int maxAperture; // open aperture fully
    int update_maxAperture; // is this a new commanded value?
    int makeHP; // set to 20 to do it, makes a new static hot pixel map
    int update_makeHP; // is this a new commanded value?
    int useHP; // use the hot pixel map to mask bad pixels
    int update_useHP; // is this a new commanded value?
    float blobParams[9]; // blobfinding parameters...
    int update_blobParams[9]; // is this a new commanded value?
    // 0 = spike limit
    // where dynamic hot pixel will designate as hp
    // 1 = dynamic hot pixels
    // (bool) search for dynamic hot pixels
    // 2 = smoothing radius
    // image smooth filter radius [px]
    // 3 = high pass filter
    // 0 == off, 1 == on
    // 4 = high pass filter radius
    // image high pass filter radius [px]
    // 5 = centroid search border
    // px dist from image edge to start star search
    // 6 = filter return image
    // 1 == true; 0 = false
    // 7 = n sigma
    // pixels > this*noise + mean = blobs
    // 8 = unique star spacing
    // min. pixel spacing between stars [px]
    // float deltaFocus; // how far to move the focus from current pos (maybe)
    int update_trigger_mode;
    int trigger_mode; // 0 for auto, 1 for software triggered
    int update_trigger_timeout_us;
    int trigger_timeout_us; // timeout between trigger checks in µs default 100
};

struct socket_data {
    char ipAddr[16];
    char port[5];
};

#endif // _SHARED_INFO_H