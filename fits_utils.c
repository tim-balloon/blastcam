// adapted from the NASA HEASARC CFITSIO cookbook:
// https://heasarc.gsfc.nasa.gov/docs/software/fitsio/cexamples/cookbook.c
/*
Copyright (Unpublished-all rights reserved under the copyright laws of the
United States), U.S. Government as represented by the Administrator of the
National Aeronautics and Space Administration. No copyright is claimed in the
United States under Title 17, U.S. Code.

Permission to freely use, copy, modify, and distribute this software and its
documentation without fee is hereby granted, provided that this copyright notice
and disclaimer of warranty appears in all copies.

DISCLAIMER:

THE SOFTWARE IS PROVIDED 'AS IS' WITHOUT ANY WARRANTY OF ANY KIND, EITHER
EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT LIMITED TO, ANY WARRANTY
THAT THE SOFTWARE WILL CONFORM TO SPECIFICATIONS, ANY IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND FREEDOM FROM
INFRINGEMENT, AND ANY WARRANTY THAT THE DOCUMENTATION WILL CONFORM TO THE
SOFTWARE, OR ANY WARRANTY THAT THE SOFTWARE WILL BE ERROR FREE. IN NO EVENT
SHALL NASA BE LIABLE FOR ANY DAMAGES, INCLUDING, BUT NOT LIMITED TO, DIRECT,
INDIRECT, SPECIAL OR CONSEQUENTIAL DAMAGES, ARISING OUT OF, RESULTING FROM, OR
IN ANY WAY CONNECTED WITH THIS SOFTWARE, WHETHER OR NOT BASED UPON WARRANTY,
CONTRACT, TORT , OR OTHERWISE, WHETHER OR NOT INJURY WAS SUSTAINED BY PERSONS OR
PROPERTY OR OTHERWISE, AND WHETHER OR NOT LOSS WAS SUSTAINED FROM, OR AROSE OUT
OF THE RESULTS OF, OR USE OF, THE SOFTWARE OR SERVICES PROVIDED HEREUNDER.
*/

#include "fits_utils.h"


/**
 * @brief Helper function to write FITS header info from metadata struct
 * 
 * @param fptr pointer to open FITS file
 * @param pMetadata pointer to struct containing FITS metadata
 * @return int 
 */
int writeMetadata(fitsfile* fptr, struct fits_metadata_t* pMetadata)
{
    // N.B. status is daisy-chained, and will fall through fitsio library
    // functions called after the first one that errors out.
    int status = 0;

    // Capture data
    fits_update_key(fptr, TSTRING, "ORIGIN", &(pMetadata->origin),
        "program used to generate file", &status);
    fits_update_key(fptr, TSTRING, "INSTRUME", &(pMetadata->instrume),
        "instrument name", &status);
    fits_update_key(fptr, TSTRING, "TELESCOP", &(pMetadata->telescop),
        "lens name", &status);
    fits_update_key(fptr, TSTRING, "OBSERVAT", &(pMetadata->observat),
        "observatory name", &status);
    fits_update_key(fptr, TSTRING, "OBSERVER", &(pMetadata->observer),
        "observer name", &status);
    fits_update_key(fptr, TSTRING, "FILENAME", &(pMetadata->filename),
        "basename + ext on disk", &status);
    fits_update_key(fptr, TSTRING, "DATE", &(pMetadata->date),
        "time of file creation (UTC)", &status);
    fits_update_key(fptr, TULONG, "UTC-SEC", &(pMetadata->utcsec),
        "time of observation start, whole seconds portion since UNIX epoch",
        &status);
    fits_update_key(fptr, TULONG, "UTC-USEC", &(pMetadata->utcusec),
        "time of observation start, microseconds portion since UNIX epoch",
        &status);
    fits_update_key(fptr, TSTRING, "FILTER", &(pMetadata->filter),
        "filter name", &status);
    fits_update_key(fptr, TFLOAT, "CCDTEMP", &(pMetadata->ccdtemp),
        "camera temp (C)", &status);
    fits_update_key(fptr, TSHORT, "FOCUS", &(pMetadata->focus),
        "focus position (encoder units)", &status);
    fits_update_key(fptr, TSHORT, "APERTURE", &(pMetadata->aperture),
        "aperture position (10x fstop)", &status);
    fits_update_key(fptr, TFLOAT, "EXPTIME", &(pMetadata->exptime),
        "total exposure time (s)", &status);
    fits_update_key(fptr, TSTRING, "BUNIT", &(pMetadata->bunit),
        "physical unit of array values", &status);

    // Compression settings
    fits_update_key(fptr, TSTRING, "FZALGOR", &(pMetadata->fzalgor),
        "fitsio compression algorithm", &status);
    fits_update_key(fptr, TSTRING, "FZTILE", &(pMetadata->fztile),
        "fitsio compression tile scheme", &status);

    // Sensor settings
    fits_update_key(fptr, TSTRING, "DETECTOR", &(pMetadata->detector),
        "sensor name", &status);
    fits_update_key(fptr, TULONG, "SENSORID", &(pMetadata->sensorid),
        "sensor unique ID", &status);
    fits_update_key(fptr, TBYTE, "BITDEPTH", &(pMetadata->bitdepth),
        "requested bit depth of camera", &status);
    fits_update_key(fptr, TFLOAT, "PIXSCAL1", &(pMetadata->pixscal1),
        "plate scale, axis 1 (arcsec/px)", &status);
    fits_update_key(fptr, TFLOAT, "PIXSCAL2", &(pMetadata->pixscal2),
        "plate scale, axis 2 (arcsec/px)", &status);
    fits_update_key(fptr, TFLOAT, "PIXSIZE1", &(pMetadata->pixsize1),
        "pixel pitch, axis 1 (micron)", &status);
    fits_update_key(fptr, TFLOAT, "PIXSIZE2", &(pMetadata->pixsize2),
        "pixel pitch, axis 2 (micron)", &status);
    fits_update_key(fptr, TFLOAT, "DARKCUR", &(pMetadata->darkcur),
        "avg dark current (e-/px/s)", &status);
    fits_update_key(fptr, TFLOAT, "RDNOISE1", &(pMetadata->rdnoise1),
        "read noise (e-)", &status);
    fits_update_key(fptr, TBYTE, "CCDBIN1", &(pMetadata->ccdbin1),
        "x-axis binning factor", &status);
    fits_update_key(fptr, TBYTE, "CCDBIN2", &(pMetadata->ccdbin2),
        "y-axis binning factor", &status);
    fits_update_key(fptr, TFLOAT, "PIXELCLK", &(pMetadata->pixelclk),
        "pixel clock (MHz)", &status);
    fits_update_key(fptr, TFLOAT, "FRAMERTE", &(pMetadata->framerte),
        "framerate (Hz)", &status);
    fits_update_key(fptr, TFLOAT, "GAINFACT", &(pMetadata->gainfact),
        "iDS gain factor setting", &status);
    fits_update_key(fptr, TFLOAT, "TRIGDLAY", &(pMetadata->trigdlay),
        "trigger delay (ms)", &status);
    fits_update_key(fptr, TUSHORT, "BLOFFSET", &(pMetadata->bloffset),
        "black level offset setting (arb units)", &status);
    fits_update_key(fptr, TLOGICAL, "AUTOGAIN", &(pMetadata->autogain),
        "automatic gain control on (1) off (0)", &status);
    fits_update_key(fptr, TLOGICAL, "AUTOEXP", &(pMetadata->autoexp),
        "automatic exposure control on (1) off (0)", &status);
    fits_update_key(fptr, TLOGICAL, "AUTOBLK", &(pMetadata->autoblk),
        "automatic black level control on (1) off (0)", &status);

    fits_report_error(stderr, status);
    return status;
}


/**
 * @brief write a 16-bit unsigned int FITS primary array image using the given
 * memory
 * 
 * @param fileName name for new FITS file
 * @param imageMem pointer to image memory, 16-bit unsigned integer
 * @param imageWidth 
 * @param imageHeight 
 * @param pMetadata pointer to struct containing FITS header values
 */
int writeImage(char* fileName, uint16_t* imageMem, uint16_t imageWidth,
    uint16_t imageHeight, struct fits_metadata_t* pMetadata)
{
    fitsfile *fptr_tmp; // pointer to the FITS file, defined in fitsio.h
    char tmpFileName[9] = "tmp.fits";
    fitsfile *fptr_comp; // compressed (final) image
    int status;
    long fpixel;
    long nelements;

    // initialize FITS image parameters
    // 16-bit unsigned short pixel values, in order to store 12bit data
    int bitpix = USHORT_IMG;
    // In FITS, NAXIS1 is the "fast" axis (x, or increasing column #), and
    // NAXIS2 is the "slow" axis (y, or increaing row #)
    long naxes[2] = {imageWidth, imageHeight};
    long naxis = 2; // 2-dimensional image

    remove(tmpFileName); // Delete old tmp file if it already exists
    remove(fileName); // Delete old final file if it already exists

    status = 0; // initialize status before calling fitsio routines

    if (fits_create_file(&fptr_tmp, tmpFileName, &status)) {
        fits_report_error(stderr, status);
        return status;
    }

    /* write the required keywords for the primary array image.     */
    /* Since bitpix = USHORT_IMG, this will cause cfitsio to create */
    /* a FITS image with BITPIX = 16 (signed short integers) with   */
    /* BSCALE = 1.0 and BZERO = 32768.  This is the convention that */
    /* FITS uses to store unsigned integers.  Note that the BSCALE  */
    /* and BZERO keywords will be automatically written by cfitsio  */
    /* in this case.                                                */

    if (fits_create_img(fptr_tmp, bitpix, naxis, naxes, &status)) {
        fits_report_error(stderr, status);
        return status;
    }

    // write header with metadata
    status = writeMetadata(fptr_tmp, pMetadata);
    if (status) {
        fits_report_error(stderr, status);
        return status;
    }

    fpixel = 1; // first pixel to write
    nelements = imageWidth * imageHeight; // number of pixels to write

    // write the array of unsigned integers to the FITS file
    if (fits_write_img(fptr_tmp, TUSHORT, fpixel, nelements, imageMem, &status)) {
        fits_report_error(stderr, status);
        return status;
    }

    // Apply compression, creating the final file
    if (fits_create_file(&fptr_comp, fileName, &status)) {
        fits_report_error(stderr, status);
        return status;
    }
    if (fits_img_compress(fptr_tmp, fptr_comp, &status)) {
        fits_report_error(stderr, status);
        return status;
    }
    // compute and write the checksum
    // to check the checksum, do `fitscheck <filename> -i` to ignore the
    // checksum missing in HDU #0 and check the compressed extension checksum in
    // HDU # 1
    if (fits_write_chksum(fptr_comp, &status)) {
        fits_report_error(stderr, status);
        return status;
    }
    if (fits_close_file(fptr_comp, &status)) {
        fits_report_error(stderr, status);
        return status;
    }
    if (fits_close_file(fptr_tmp, &status)) {
        fits_report_error(stderr, status);
        return status;
    }
    remove(tmpFileName);

    return status;
}


// /**
//  * @brief read a FITS image into the given memory
//  * @details for the purposes of this code, get just the image data, not any
//  * header data.
//  * 
//  */
// int readImage(char* fileName, uint16_t* imageMem, uint16_t imageWidth,
//     uint16_t imageHeight)
// {
//     fitsfile *fptr; // pointer to the FITS file, defined in fitsio.h
//     int status;
//     int nfound;
//     int anynull;
//     long naxes[2];
//     long fpixel;
//     long nbuffer;
//     long npixels;
//     long ii;

//     float datamin;
//     float datamax;
//     float nullval;
//     // TODO(evanmayer): change buffer type to uint16_t
//     float buffer[FITS_BUFF_SIZE];

//     status = 0;

//     if (fits_open_file(&fptr, fileName, READONLY, &status)) {
//         fits_report_error(stderr, status);
//         return status;
//     }

//     // read the NAXIS1 and NAXIS2 keyword to get image size
//     if (fits_read_keys_lng(fptr, "NAXIS", 1, 2, naxes, &nfound, &status)) {
//         fits_report_error(stderr, status);
//         return status;
//     }

//     npixels  = naxes[0] * naxes[1]; // number of pixels in the image
//     fpixel   = 1;
//     nullval  = 0; // don't check for null values in the image
//     datamin  = 1.0E30f;
//     datamax  = -1.0E30f;

//     while (npixels > 0) {
//         nbuffer = npixels;
//         // read as many pixels as will fit in buffer
//         if (npixels > FITS_BUFF_SIZE) {
//             nbuffer = FITS_BUFF_SIZE;
//         }

//         /* Note that even though the FITS images contains unsigned integer */
//         /* pixel values (or more accurately, signed integer pixels with    */
//         /* a bias of 32768),  this routine is reading the values into a    */
//         /* float array.   Cfitsio automatically performs the datatype      */
//         /* conversion in cases like this.                                  */

//         if (fits_read_img(fptr, TFLOAT, fpixel, nbuffer, &nullval,
//             buffer, &anynull, &status)) {
//             fits_report_error(stderr, status);
//             return status;
//         }
//         // TODO(evanmayer): replace processing logic with reading
//         for (ii = 0; ii < nbuffer; ii++) {
//             if (buffer[ii] < datamin) {
//                 datamin = buffer[ii];
//             }
//             if (buffer[ii] > datamax) {
//                 datamax = buffer[ii];
//             }
//         }
//         npixels -= nbuffer; // increment remaining number of pixels
//         fpixel += nbuffer; // next pixel to be read in image
//     }

//     if (fits_close_file(fptr, &status)) {
//         fits_report_error(stderr, status);
//         return status;
//     }

//     return status;
// }
