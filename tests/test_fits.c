#include "../fits_utils.h"

#define TEST_FITS_IMG_WIDTH 1200
#define TEST_FITS_IMG_HEIGHT 1200

int main(int argc, int* argv)
{
    struct fits_metadata_t metadata = default_metadata;

    char fileName[13] = "test.fits.gz";
    uint16_t imageMem[TEST_FITS_IMG_WIDTH * TEST_FITS_IMG_HEIGHT] = {0};

    imageMem[(TEST_FITS_IMG_WIDTH * TEST_FITS_IMG_HEIGHT + TEST_FITS_IMG_WIDTH) / 2] = 4095;

    writeImage(fileName, imageMem, TEST_FITS_IMG_WIDTH, TEST_FITS_IMG_HEIGHT, &metadata);
}