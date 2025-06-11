#include "../fits_utils.h"

#define TEST_FITS_IMG_WIDTH 1200
#define TEST_FITS_IMG_HEIGHT 1200

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
    .autoblk = 0,
};

int main(int argc, int* argv)
{
    char fileName[13] = "test.fits.gz";
    uint16_t imageMem[TEST_FITS_IMG_WIDTH * TEST_FITS_IMG_HEIGHT] = {0};

    imageMem[(TEST_FITS_IMG_WIDTH * TEST_FITS_IMG_HEIGHT + TEST_FITS_IMG_WIDTH) / 2] = 4095;

    writeImage(fileName, imageMem, TEST_FITS_IMG_WIDTH, TEST_FITS_IMG_HEIGHT,
        &default_metadata);
}