#ifndef LENS_ADAPTER_H
#define LENS_ADAPTER_H

// The average of the peak sharpness location of 14 double-pass AF runs
// Sigma 85mm f/1.4 HSM DG ART lens
// Outside air temperature = 94F
// Red filter B+W 091
#define DEFAULT_FOCUS_OFFSET (-514)

int initLensAdapter(char * path);
int beginAutoFocus();
int defaultFocusPosition();
int shiftFocus(char * cmd);
int calculateOptimalFocus(int num_focus, char * auto_focus_file);
int adjustCameraHardware();
int runCommand(const char * command, int file, char * return_str);

#pragma pack(push, 1)
/* Camera and lens parameter struct, including auto-focusing */
struct camera_params {
    // focus and aperture fields
    int prev_focus_pos;
    int focus_position;
    int focus_inf;
    int aperture_steps;
    int max_aperture;
    int min_focus_pos;
    int max_focus_pos;
    int current_aperture;
    // camera parameter, not lens parameter, but adjustments to it are made in 
    // lens_adapter.c
    double exposure_time;
    int change_exposure_bool;
    double gainfact;
    int change_gainfact_bool;
    // auto-focusing parameters & data
    int begin_auto_focus;       // flag to enter auto-focusing
    int focus_mode;             // state of auto-focusing (1 = on, 0 = off)
    int start_focus_pos;        // where to start the auto-focusing process
    int end_focus_pos;          // where to end the auto-focusing process
    int focus_step;             // granularity of auto-focusing checker
    int photos_per_focus;       // number of photos per auto-focusing position
    double flux;                   // brightness of most recent maximum flux
};
#pragma pack(pop)

extern struct camera_params all_camera_params;

#endif 