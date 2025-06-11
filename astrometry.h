#ifndef ASTROMETRY_H
#define ASTROMETRY_H


int initAstrometry();
void closeAstrometry();
int lostInSpace(double * star_x, double * star_y, double * star_mags, 
                unsigned num_blobs, struct tm * tm_info, char * datafile);

/* Astrometry parameters and solutions struct */
#pragma pack(push, 1)
struct astrometry {
    double timelimit;
    double rawtime;
    // for solving altaz
    double logodds;
    double latitude;
    double longitude;
    double hm;
    // astrometry solution
    double ra;
    double dec;
    double fr;
    double ps;
    double ir;
    double alt;
    double az;
    double sigma_pointing_as;
    double dec_j2000;
    double ra_j2000;
    double photo_time;
};
#pragma pack(pop)


extern struct mcp_astrometry mcp_astro;

extern struct astrometry all_astro_params;

#endif