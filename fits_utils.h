#ifndef _FITS_UTILS_H
#define _FITS_UTILS_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "fitsio.h"

#define FITS_BUFF_SIZE 1000


// Container for metadata to put into FITS files.
// These are metadata that are IN ADDITION TO the ones required to save a
// file (SIMPLE, BITPIX, etc.)
// Some members are constants of this application, and cannot be modified, while
// some are meant to be changed on the fly. Each member corresponds to a FITS
// header keyword.
// This list of items is modified from the Kuiper 61-inch Mont4K system.
struct fits_metadata_t {

    // Capture data

    char origin[10]; // ORIGIN: program used to generate file
    char instrume[10]; // INSTRUME: instrument name
    char telescop[30]; // TELESCOP: lens name
    char observat[10]; // OBSERVAT: observatory name
    char observer[10]; // OBSERVER: observer name
    char filename[256]; // FILENAME: basename + ext on disk
    char date[256]; // DATE: time of file creation (UTC) to nearest second
    uint64_t utcsec; // UTC-SEC: time of observation start, whole seconds portion since UNIX epoch
    uint64_t utcusec; // UTC-USEC: time of observation start, microseconds portion since UNIX epoch
    char filter[20]; // FILTER: filter name
    float ccdtemp; // CCDTEMP: camera temp (C)
    int16_t focus; // FOCUS: focus position (encoder units)
    int16_t aperture; // APERTURE: aperture position (10x fstop)
    float exptime; // EXPTIME: total exposure time (s)
    char bunit[4]; // BUNIT: physical unit of array values (ADU)

    // Compression settings, not all are used for integer images
    // https://heasarc.gsfc.nasa.gov/docs/software/fitsio/c/c_user/node41.html

    char fzalgor[12]; // FZALGOR  - 'RICE_1' , 'GZIP_1', 'GZIP_2', 'HCOMPRESS_1', 'PLIO_1', 'NONE'
    char fztile[4]; // FZTILE   - 'ROW', 'WHOLE', or '(n,m)'

    // Sensor settings

    char detector[64]; // DETECTOR: sensor name
    uint64_t sensorid; // SENSORID: camera unique numerical identifier
    uint8_t bitdepth; // BITDEPTH: requested bit depth of camera, not equivalent to BITPIX
    float pixscal1; // PIXSCAL1: plate scale, axis 1 (arcsec/px)
    float pixscal2; // PIXSCAL2: plate scale, axis 2 (arcsec/px)
    float pixsize1; // PIXSIZE1: pixel pitch, axis 1 (micron)
    float pixsize2; // PIXSIZE2: pixel pitch, axis 2 (micron)
    float darkcur; // DARKCUR: avg dark current (e-/px/s)
    float rdnoise1; // RDNOISE1: read noise (e-)
    uint8_t ccdbin1; // CCDBIN1: x-axis binning factor
    uint8_t ccdbin2; // CCDBIN2: y-axis binning factor
    float pixelclk; // PIXELCLK: pixel clock (MHz)
    float framerte; // FRAMERTE: framerate (Hz)
    float gainfact; // GAINFACT: iDS gain factor setting (e.g. 2.0x)
    // GAIN1: sensor gain, e-/DN...depends on GAINFACT, not known a-priori unless calibrated
    float trigdlay; // TRIGDLAY: trigger delay (ms)
    uint16_t bloffset; // BLOFFSET: black level offset setting, arb units
    int8_t autogain; // AUTOGAIN: automatic gain control on (1) off (0)
    int8_t autoexp; // AUTOEXP: automatic exposure control on (1) off (0)
    int8_t autoblk; // AUTOBLK: automatic black level offset on (1) off (0)

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

// Provide a default initialization to the application code as a template to
// update as needed.
extern struct fits_metadata_t default_metadata;

int writeImage(char* fileName, uint16_t* imageMem,
    uint16_t imageWidth, uint16_t imageHeight,
    struct fits_metadata_t* pMetadata);
int writeMetadata(fitsfile* fptr, struct fits_metadata_t* pMetadata);
int readImage(char* fileName, uint16_t* imageMem,
    uint16_t imageWidth, uint16_t imageHeight);

#endif // _FITS_UTILS_H