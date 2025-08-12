#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../convolve.h"

#define CLOSE 1e-6
bool verbose = 1;

#define IMAGE_WIDTH 5320
#define IMAGE_HEIGHT 3032

float imageBuffer[IMAGE_WIDTH * IMAGE_HEIGHT] = {0};
unsigned char mask[IMAGE_WIDTH * IMAGE_HEIGHT] = {0};
float imageResult[IMAGE_WIDTH * IMAGE_HEIGHT] = {0};

#define CAMERA_WIDTH 9//5320
#define CAMERA_HEIGHT 9//3032
#define CAMERA_NUM_PX 81//5320 * 3032
uint16_t imageBufferB[CAMERA_WIDTH * CAMERA_HEIGHT] = {0};
double imageResultB[CAMERA_WIDTH * CAMERA_HEIGHT] = {0};
/**
 * @brief Function that will do a simple boxcar smoothing of an image.
 * 
 * @param ib "input buffer" the input image with 12 bit depth, stored in 16bit ints
 * @param i0 starting column for filtering
 * @param j0 starting row for filtering
 * @param i1 ending column for filtering
 * @param j1 ending row for filtering
 * @param r_f boxcar filter radius
 * @param filtered_image output image
 */
void boxcarFilterImage(uint16_t * ib, int i0, int j0, int i1, int j1, int r_f, 
                       double * filtered_image)
{
    static int first_time = 1;
    static char * nc = NULL;
    static uint64_t * ibc1 = NULL;

    if (first_time) {
        nc = calloc(CAMERA_NUM_PX, 1);
        ibc1 = calloc(CAMERA_NUM_PX, sizeof(uint64_t));
        first_time = 0;
    }

    int b = r_f;
    int64_t isx;
    int s, n;
    double ds, dn;
    double last_ds = 0;

    for (int j = j0; j < j1; j++) {
        n = 0;
        isx = 0;
        for (int i = i0; i < i0 + 2*r_f + 1; i++) {
            n += mask[i + j*CAMERA_WIDTH];
            isx += ib[i + j*CAMERA_WIDTH]*mask[i + j*CAMERA_WIDTH];
        }

        int idx = CAMERA_WIDTH*j + i0 + r_f;

        for (int i = r_f + i0; i < i1 - r_f - 1; i++) {
            ibc1[idx] = isx;
            nc[idx] = n;
            isx = isx + mask[idx + r_f + 1]*ib[idx + r_f + 1] - 
                  mask[idx - r_f]*ib[idx - r_f];
            n = n + mask[idx + r_f + 1] - mask[idx - r_f];
            idx++;
        }

        ibc1[idx] = isx;
        nc[idx] = n;
    }

    for (int j = j0+b; j < j1-b; j++) {
        for (int i = i0+b; i < i1-b; i++) {
            n = s = 0;
            for (int jp =- r_f; jp <= r_f; jp++) {
                int idx = i + (j+jp)*CAMERA_WIDTH;
                s += ibc1[idx];
                n += nc[idx];
            }
            ds = s;
            dn = n;
            if (dn > 0.0) {
                ds /= dn;
                last_ds = ds;
            } else {
                ds = last_ds;
            }
            filtered_image[i + j*CAMERA_WIDTH] = ds;
        }
    }
}


int saveArrayDumb(char* fname, float* imageResult, uint16_t imageWidth, uint32_t imageNumPix) {
    bool isRight = 0;
    FILE* fp;
    if ((fp = fopen(fname, "w")) == NULL) {
        fprintf(stderr, "Could not open image result file %s: %s.\n",
            fname, strerror(errno));
        return -1;
    }
    for (int i = 0; i < imageNumPix; i++)
    {
        isRight = (0 == ((i + 1) % imageWidth));
        fprintf(fp, "%.6f", imageResult[i]);
        if (isRight)
        {
            fprintf(fp,"%s","\n");
        } else {
            fprintf(fp,"%s",",");
        }
    }
    fflush(fp);
    fclose(fp);
    return 0;
}


// reset buffer and result allocated memeory
void reset(void) {
    memset(imageBuffer, 0.0, sizeof(imageBuffer));
    memset(mask, 1.0, sizeof(mask));
    memset(imageResult, 0.0, sizeof(imageResult));
    memset(imageBufferB, 0.0, sizeof(imageBufferB));
    memset(imageResultB, 0.0, sizeof(imageResultB));
}

// Test results of 3x3 Gaussian blur
void test_doConvolution3x3_Gaussian(void) {
    printf("\ntest_doConvolution3x3_Gaussian\n");

    reset();

    uint16_t imageWidth = 9;
    uint16_t imageHeight = 9;
    uint32_t imageNumPix = imageWidth * imageHeight;
    
    for (unsigned int i = 0; i < imageNumPix; i++) {
        mask[i] = 1;
    }
    imageBuffer[imageNumPix / 2] = 100.0;

    float gaussianKernel[9] = {1./16., 2./16., 1./16., 2./16., 4./16., 2./16., 1./16., 2./16., 1./16.}; // gaussian

    doConvolution3x3(imageBuffer, mask, imageWidth, imageNumPix,
        gaussianKernel, imageResult);

    float answers[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 6.25, 12.5, 6.25, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 12.5, 25.0, 12.5, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 6.25, 12.5, 6.25, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%06.3f - %06.3f],", imageResult[i], answers[i]);
        }
        assert (fabs(imageResult[i] - answers[i]) < CLOSE);
    }
    printf("\nPASS\n");}


// A big array for testing 3x3 case optimization
void test_doConvolution3x3_perf(void) {
    printf("\ntest_doConvolution3x3_perf\n");

    reset();

    uint16_t imageWidth = 5320;
    uint16_t imageHeight = 3032;
    uint32_t imageNumPix = imageWidth * imageHeight;

    float gaussianKernel[9] = {1./16., 2./16., 1./16., 2./16., 4./16., 2./16., 1./16., 2./16., 1./16.}; // gaussian
    // technically, sobel takes G = (G_x^2 + G_y^2)^.5 as an approximation of
    // the gradient, but stars are round, so any stars (or indeed other
    // features) will be sharp in x,y, or any other direction.
    float sobelKernel[9] = {-1., 0., 1., -2., 0., 2., -1., 0., 1.}; // sobel x

    struct timespec tstart = {0,0};
    struct timespec tconv = {0,0};
    struct timespec tend = {0,0};

    int nCalls = 10;
    while (nCalls > 0) {
        nCalls -= 1;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tstart);

        doConvolution3x3(imageBuffer, mask, imageWidth, imageNumPix,
            gaussianKernel, imageResult);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tconv);

        printf("doConvolution took about %.7f seconds to do conv\n",
            ((double)tconv.tv_sec + 1.0e-9*tconv.tv_nsec) - 
            ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec)
        );
    }

    saveArrayDumb("dummyArrayBig.csv", imageResult, imageWidth, imageNumPix);

}


void test_boxcarFilterImage_3x3(void) {
    printf("\ntest_boxcarFilterImage_3x3\n");

    reset();

    int imageWidth = CAMERA_WIDTH;
    int imageHeight = CAMERA_HEIGHT;
    uint32_t imageNumPix = imageWidth * imageHeight;
    int radius = 1;

    imageBufferB[imageNumPix / 2] = 101.0;
    imageBufferB[imageNumPix / 2 + 1] = 101.0;

    boxcarFilterImage(imageBufferB, 0, 0, imageWidth, imageHeight, radius, 
            imageResultB);

    double answersH[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 101./9., 2*101./9., 2*101./9., 101./9., 0.00, 0.00,
        0.00, 0.00, 0.00, 101./9., 2*101./9., 2*101./9., 101./9., 0.00, 0.00,
        0.00, 0.00, 0.00, 101./9., 2*101./9., 2*101./9., 101./9., 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%06.3f - %06.3f],", imageResultB[i], answersH[i]);
        }
        assert (fabs(imageResultB[i] - answersH[i]) < CLOSE);
    }
    printf("\nPASS\n");

    reset();

    imageBufferB[imageNumPix / 2 - imageWidth] = 101.0;
    imageBufferB[imageNumPix / 2] = 101.0;

    boxcarFilterImage(imageBufferB, 0, 0, imageWidth, imageHeight, radius, 
            imageResultB);

    double answersV[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 101./9, 101./9, 101./9., 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 2*101./9., 2*101./9., 2*101./9., 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 2*101./9., 2*101./9., 2*101./9., 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 101./9., 101./9., 101./9., 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%06.3f - %06.3f],", imageResultB[i], answersV[i]);
        }
        assert (fabs(imageResultB[i] - answersV[i]) < CLOSE);
    }
    printf("\nPASS\n");


}


void test_boxcarFilterImage_edge(void) {
    printf("\ntest_boxcarFilterImage_edge\n");

    int imageWidth = CAMERA_WIDTH;
    int imageHeight = CAMERA_HEIGHT;
    uint32_t imageNumPix = imageWidth * imageHeight;
    int radius = 1;

    // ========================================================================
    // LEFT
    // ========================================================================
    reset();

    imageBufferB[imageNumPix * 2] = 101.0;

    boxcarFilterImage(imageBufferB, 0, 0, imageWidth, imageHeight, radius, 
            imageResultB);

    double Lanswers[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%09.6f]", imageResultB[i] - Lanswers[i]);
        }
        assert (fabs(imageResultB[i] - Lanswers[i]) < CLOSE);
    }

    printf("\nPASS\n");

    // ========================================================================
    // TOP LEFT
    // ========================================================================
    reset();

    imageBufferB[0] = 101.0;

    boxcarFilterImage(imageBufferB, 0, 0, imageWidth, imageHeight, radius, 
            imageResultB);

    float TLanswers[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 101./9., 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%012.9f]", imageResultB[i] - TLanswers[i]);
        }
        assert (fabs(imageResultB[i] - TLanswers[i]) < CLOSE);
    }

    printf("\nPASS\n");

    // ========================================================================
    // TOP RIGHT
    // ========================================================================
    reset();

    imageBufferB[imageWidth - 1] = 101.0;
    boxcarFilterImage(imageBufferB, 0, 0, imageWidth, imageHeight, radius, 
            imageResultB);

    double TRanswers[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 101./9., 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%09.6f]", imageResultB[i] - TRanswers[i]);
        }
        assert (fabs(imageResultB[i] - TRanswers[i]) < CLOSE);
    }

    printf("\nPASS\n");

    // ========================================================================
    // BOTTOM LEFT
    // ========================================================================
    reset();

    imageBufferB[imageNumPix - imageWidth] = 101.0;
    boxcarFilterImage(imageBufferB, 0, 0, imageWidth, imageHeight, radius, 
            imageResultB);

    double BLanswers[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 101./9., 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%09.6f]", imageResultB[i] - BLanswers[i]);
        }
        assert (fabs(imageResultB[i] - BLanswers[i]) < CLOSE);
    }

    printf("\nPASS\n");

    // ========================================================================
    // BOTTOM RIGHT
    // ========================================================================
    reset();

    imageBufferB[imageNumPix - 1] = 101.0;
    boxcarFilterImage(imageBufferB, 0, 0, imageWidth, imageHeight, radius, 
            imageResultB);

    double BRanswers[81] = {
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 101./9., 0.00,
        0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00,
    };

    for (unsigned int i = 0; i < imageNumPix; i++) {
        if (verbose) {
            if (0 == i % imageWidth) {
                printf("\n");
            }
            printf("[%09.6f]", imageResultB[i] - BRanswers[i]);
        }
        assert (fabs(imageResultB[i] - BRanswers[i]) < CLOSE);
    }

    printf("\nPASS\n");
}


int main(int argc, char* argv[]) {
    test_doConvolution3x3_Gaussian();
    // test_doConvolution3x3_perf();

    test_boxcarFilterImage_3x3();
    test_boxcarFilterImage_edge();

    return 0;
}
