#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <astrometry/os-features.h>
#include <astrometry/engine.h>
#include <astrometry/solver.h>
#include <astrometry/index.h>
#include <astrometry/starxy.h>
#include <astrometry/matchobj.h>
#include <astrometry/healpix.h>
#include <astrometry/bl.h>
#include <astrometry/log.h>
#include <astrometry/errors.h>
#include <astrometry/fileutils.h>
#include <sofa.h>

#include "camera.h"
#include "astrometry.h"
#include "lens_adapter.h"
#include "commands.h"
#include "sc_data_structures.h"

#define _USE_MATH_DEFINES
/* Longitude and latitude constants (deg) */
#define backyard_lat  32.233315
#define backyard_long -110.948556
#define backyard_hm   753.8

engine_t * engine = NULL;
solver_t * solver = NULL;
int solver_timelimit;


/* Astrometry parameters global structure, accessible from commands.c as well */
struct astrometry all_astro_params = {
    .timelimit = 1,
    .rawtime = 0,
    .numBlobsFound = 0,
    .logodds = 1e8,
    .latitude = backyard_lat,
    .longitude = backyard_long,
    .hm = backyard_hm,
    .ra = 0, 
    .dec = 0,
    .fr = 0,
    .ps = 0,
    .ir = 0,
    .alt = 0,
    .az = 0,
    .sigma_pointing_as = 0,
};

/* Function to decrement a counter for tracking Astrometry timeout.
** Input: The pointer to the counter.
** Output: If the counter has reached zero yet (or not).
*/
time_t timeout(void * arg) {
    int * counter = (int *) arg;

    if (*(counter) != 0) {
        if (verbose) {
            printf("Have yet to solve Astrometry. Decrementing time "
                   "counter...\n");
        }

        (*counter)--;

        if (verbose) {
            printf("Timeout counter is now %d.\n", *counter);
        }
    } 

    // if we are shutting down, we don't want to keep trying to solve (we just
    // want to abort altogether)
    if (shutting_down) {
        if (verbose) {
            printf("Shutting down -> zeroing timeout counter.\n");
        }
        *counter = 0;
    }

    return (*counter != 0);
}


/* Function to initialize astrometry.
** Input: None.
** Output: Flag indicating successful initialization of Astrometry system
*/
int initAstrometry() {
    engine = engine_new();
    solver = solver_new();

    if (verbose) {
        printf("initAstrometry: initializing...\n");
    }
    
    if (engine_parse_config_file(engine, 
                                 "/usr/local/astrometry/etc/astrometry.cfg")) {
        printf("Bad configuration file in Astrometry constructor.\n");
        return -1;
    }

    // set solver timeout
    solver_timelimit = (int) all_astro_params.timelimit;
    solver->timer_callback = timeout;
    solver->userdata = &solver_timelimit;

    return 1;
}

/* Function to close astrometry.
** Input: None.
** Output: None (void).
*/
void closeAstrometry() {	
    if (verbose) {
        printf("Closing Astrometry...\n");
    }
    engine_free(engine);
    solver_free(solver);
}

/* Function for solving for pointing location on the sky.
** Input: x coordinates of the stars (star_x), y coordinates of the stars 
** (star_y), magnitudes of the stars (star_mags), the number of blobs, timing 
** structure, and the observing file name.
** Output: the status of finding a solution or not (sol_status).
*/
int lostInSpace(double * star_x, double * star_y, double * star_mags, unsigned 
                num_blobs, struct tm * tm_info, char * datafile) {
    int sol_status;
    // timers for astrometry
    struct timespec astrom_tp_beginning, astrom_tp_end; 
    double hprange, start, end, astrom_time;
    double ra, dec, fr, ps, ir, sigma_pointing;
    // for apportioning Julian dates
    double d1, d2;
    // 'ob' means observed (observed frame versus ICRS frame)
    double aob, zob, hob, dob, rob, eo;
    FILE * fptr = NULL;

    // reset solver timeout
    solver_timelimit = (int) all_astro_params.timelimit;
    if (verbose) {
        printf("Astrom. timeout is %i cycles.\n", *((int *) solver->userdata));
    }

    // set up solver configuration
    solver->funits_lower = MIN_PS;
    solver->funits_upper = MAX_PS;
    
    // set max number of sources
    unsigned int original_num_blobs = num_blobs;
    if (num_blobs > MAX_BLOBS) {
        num_blobs = MAX_BLOBS;
    }
    solver->endobj = num_blobs;
    all_astro_params.numBlobsFound = original_num_blobs;

    // disallow tiny quads
    solver->quadsize_min = 0.1*MIN(CAMERA_WIDTH - 2*CAMERA_MARGIN, 
                                   CAMERA_HEIGHT - 2*CAMERA_MARGIN);

    // set parity which can speed up x2
    solver->parity = PARITY_BOTH; 
    
    // sets the odds ratio we will accept (logodds parameter)
    solver_set_keep_logodds(solver, log(all_astro_params.logodds));  

    solver->logratio_totune = log(1e6);
    solver->logratio_toprint = log(1e6);
    solver->distance_from_quad_bonus = 1;

    // figure out the index file range to search in
    hprange = arcsec2dist(MAX_PS*hypot(CAMERA_WIDTH - 2*CAMERA_MARGIN, 
                                       CAMERA_HEIGHT - 2*CAMERA_MARGIN)/2.0);

    // make list of stars
    starxy_t * field = starxy_new(num_blobs, 1, 0);

    // start timer for astrometry 
    if (clock_gettime(CLOCK_REALTIME, &astrom_tp_beginning) == -1) {
        fprintf(stderr, "Unable to start timer: %s.\n", strerror(errno));
    }

    starxy_set_x_array(field, star_x);
    starxy_set_y_array(field, star_y);
    starxy_set_flux_array(field, star_mags);
    starxy_sort_by_flux(field);

    solver_set_field(solver, field);
    solver_set_field_bounds(solver, 0, CAMERA_WIDTH - 2*CAMERA_MARGIN, 0, 
                            CAMERA_HEIGHT - 2*CAMERA_MARGIN);

    // add index files
    for (int i = 0; i < (int) pl_size((*engine).indexes); i++) {
        index_t * index = (index_t *) pl_get((*engine).indexes, i);
        solver_add_index(solver, index);
        index_reload(index);
    }

    solver_log_params(solver);
    solver_run(solver);

    // solution status should be 0 since we have yet to achieve a solution 
    sol_status = 0;
    if ((fptr = fopen(datafile, "a")) == NULL) {
        fprintf(stderr, "Could not open observing file: %s.\n", 
                strerror(errno));
        return sol_status;
    }
    if ((*solver).best_match_solves) {
        double pscale;
        tan_t * wcs;

        // get World Coordinate System data (wcs)
        wcs = &((*solver).best_match.wcstan);
        tan_pixelxy2radec(wcs, (CAMERA_WIDTH - 2*CAMERA_MARGIN - 1)/2.0, 
                               (CAMERA_HEIGHT - 2*CAMERA_MARGIN - 1)/2.0, &ra, 
                               &dec);
        
        // calculate pixel scale and field rotation
        ps = tan_pixel_scale(wcs);
        fr = tan_get_orientation(wcs); 

        // calculate Julian date
        if (iauDtf2d("UTC", tm_info->tm_year + 1900, tm_info->tm_mon + 1, 
                            tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min,
                            (double) tm_info->tm_sec, &d1, &d2) != 0) {
            printf("Julian date not properly calculated.\n");
            return sol_status;
        }

        // calculate AltAz
        if (iauAtco13(ra*(M_PI/180.0), dec*(M_PI/180.0), 0.0, 0.0, 0.0, 0.0, d1,
                      d2 + (all_camera_params.exposure_time/(2000.0*3600.0*24.0)), 
                      dut1, all_astro_params.longitude*(M_PI/180.0), 
                      all_astro_params.latitude*(M_PI/180.0), 
                      all_astro_params.hm, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                      &aob, &zob, &hob, &dob, &rob, &eo) != 0) {
            printf("Review preceding Julian date calculation; dubious year or "
                   "unacceptable date passed to AltAz calculation.\n");
            return sol_status;
        }

        // calculate parallactic angle and add it to field rotation to get image
        // rotation
        ir = (iauHd2pa(hob, dob, 
                       all_astro_params.latitude*(M_PI/180.0)))*(180.0/M_PI) - fr;

        // Calculate the RMS uncertainty for the particular starfield that was
        // just matched
        // Get the ra, dec positions of the best-matching database field of
        // stars from their unit sphere xyz coords
        MatchObj* mo = &((*solver).best_match);
        mo->refradec = malloc(3 * mo->nindex * sizeof(double));
        for (uint i = 0; i < mo->nindex; i++)
        {
            xyzarr2radecdegarr(mo->refxyz + i * 3, mo->refradec + i * 2);
        }
        // generate the sip from the wsctan
        sip_t sip;
        sip_wrap_tan(wcs, &sip);

        double sum_sq_diffs = 0;
        int counter = 0;
        for (uint j = 0; j < solver->best_match.nfield; j++)
        {
            // Unpack the reference star locations from the best match object and translate them to XY coordinates
            double fx, fy, rx, ry, refRA, refDec;
            int ti, ri;
            ri = mo->theta[j];
            if (ri < 0)
            {
                continue;
            }
            ti = j;
            refRA = mo->refradec[2*ri+0];
            refDec = mo->refradec[2*ri+1];
            if (!sip_radec2pixelxy(&sip, refRA, refDec, &rx, &ry))
            {
                continue;
            }
            // Get the pixel positions of the blobs we found
            fx = solver->fieldxy->x[ti];
            fy = solver->fieldxy->y[ti];
            
            // rolling total of the sum of the square differences
            sum_sq_diffs += ((fx - rx) * (fx - rx)) + ((fy - ry) * (fy - ry));
            counter++;
        }

        // Convert sum of square differences into RMS uncertainty 
        sigma_pointing = sqrt(sum_sq_diffs / counter);
        double sigma_pointing_as = sigma_pointing * ps;

        // end timer
        if (clock_gettime(CLOCK_REALTIME, &astrom_tp_end) == -1) {
            fprintf(stderr, "Error ending timer: %s.\n", strerror(errno));
        }
        // update astro struct with telemetry
        all_astro_params.dec_j2000 = dec;
        all_astro_params.ra_j2000 = ra;
        all_astro_params.ir = ir;
        all_astro_params.ra = rob*(180.0/M_PI);
        all_astro_params.dec = dob*(180.0/M_PI);
        all_astro_params.alt = 90.0 - (zob*(180.0/M_PI));
        all_astro_params.az = aob*(180.0/M_PI); 
        all_astro_params.fr = fr;
        all_astro_params.ps = ps;
        all_astro_params.sigma_pointing_as = sigma_pointing_as;

        printf("\n+---------------------------------------------------------+\n");
        printf("|\t\tTelemetry\t\t\t\t  |\n");
        printf("|---------------------------------------------------------|\n");
        printf("|\tRaw time (sec): %.1f\t\t\t  |\n", all_astro_params.rawtime);
        printf("|\tNumber of blobs found: %i\t\t\t  |\n", original_num_blobs);
        printf("|\tNumber of blobs used: %i\t\t\t  |\n", num_blobs);
        printf("|\tAstrometry RA (deg): %lf\t\t\t  |\n", ra);
        printf("|\tAstrometry DEC (deg): %lf\t\t\t  |\n", dec);
        printf("|\tObserved RA (deg): %lf\t\t\t  |\n", all_astro_params.ra);
        printf("|\tObserved DEC (deg): %lf\t\t\t  |\n", all_astro_params.dec);
        printf("|\tField rotation (deg): %f\t\t  |\n", all_astro_params.fr);
        printf("|\tImage rotation (deg): %lf\t\t  |\n", all_astro_params.ir);
        printf("|\tPixel scale (arcsec/px): %lf\t\t  |\n", all_astro_params.ps);
        printf("|\tAltitude (deg): %.15f\t\t  |\n", all_astro_params.alt);
        printf("|\tAzimuth (deg): %.15f\t\t  |\n", all_astro_params.az);
        printf("|\tPointing uncertainty (arcsec): %.3lf\t\t  |\n", all_astro_params.sigma_pointing_as);
        printf("+---------------------------------------------------------+\n\n");


        // calculate how long solution took to solve in terms of nanoseconds
        start = (double) (astrom_tp_beginning.tv_sec*1e9) + (double) astrom_tp_beginning.tv_nsec;
        end = (double) (astrom_tp_end.tv_sec*1e9) + (double) astrom_tp_end.tv_nsec;
            astrom_time = end - start;
        printf("Astrometry solved in %f msec.\n", astrom_time*1e-6);

        // write astrometry solution to data.txt file
        if (verbose) {
            printf(" > Writing Astrometry solution to data file...\n");
        }

        if (fprintf(fptr, "%i,%lf,%lf,%lf,%lf,%lf,%lf,%.15f,%.15f,%lf,%f,%.15lf", num_blobs,
                        ra, dec,
                          all_astro_params.ra, all_astro_params.dec, 
                          all_astro_params.fr, all_astro_params.ps, 
                          all_astro_params.alt, all_astro_params.az, 
                          all_astro_params.ir, astrom_time*1e-6,
                        all_astro_params.sigma_pointing_as) < 0) {
              fprintf(stderr, "Error writing solution to observing file: %s.\n", 
                  strerror(errno));
          }
    
        fflush(fptr);
        fclose(fptr);

        // we achieved a solution!
        sol_status = 1;
    } else {
        // if no solution was found, write a line of 0s to the data file for ease of post-run data analysis
        if (fprintf(fptr, "0,0,0,0,0,0,0,0,0,0,0,0") < 0) {
            fprintf(stderr, "Unable to write time and blob count to observing file: %s.\n", strerror(errno));
            }
        fflush(fptr);
        fclose(fptr);
    }
    
    // clean everything up and return the status
    solver_cleanup_field(solver);
    solver_clear_indexes(solver);

    return sol_status;
}
