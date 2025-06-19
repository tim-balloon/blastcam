#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../convolve.h"

#define IMAGE_WIDTH 5320
#define IMAGE_HEIGHT 3032
//#define IMAGE_WIDTH 11
//#define IMAGE_HEIGHT 11

uint16_t imageBuffer[IMAGE_WIDTH * IMAGE_HEIGHT] = {0};
unsigned char mask[IMAGE_WIDTH * IMAGE_HEIGHT] = {0};
float imageResult[IMAGE_WIDTH * IMAGE_HEIGHT] = {0};


int saveArrayDumb(float* imageResult, uint16_t imageWidth, uint32_t imageNumPix) {
    bool isRight = 0;
    FILE* fp;
    if ((fp = fopen("dummyArray.csv", "w")) == NULL) {
        fprintf(stderr, "Could not open image result file: %s.\n",
            strerror(errno));
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


int main(int argc, char* argv[]) {
    uint16_t imageWidth = IMAGE_WIDTH;
    uint16_t imageHeight = IMAGE_HEIGHT;
    uint32_t imageNumPix = imageWidth * imageHeight;
    
    for (unsigned int i = 0; i < IMAGE_WIDTH * IMAGE_HEIGHT; i++) {
        mask[i] = 1;
    }
    imageBuffer[imageNumPix / 2] = 100;

    uint16_t kernel[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1}; // gaussian
    // technically, sobel takes G = (G_x^2 + G_y^2)^.5 as an approximation of
    // the gradient, but stars are round, so any stars (or indeed other
    // features) will be sharp in x,y, or any other direction.
    // int16_t kernel[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1}; // sobel x
    uint16_t kernelSize = 9;

    struct timespec tstart = {0,0};
    struct timespec tconv = {0,0};
    struct timespec tend = {0,0};

    uint16_t fakeMean = 0; // not actually the mean, but fulfills signature
    int nCalls = 10;
    while (nCalls > 0) {
        nCalls -= 1;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tstart);

        doConvolution(imageBuffer, fakeMean, imageWidth, imageNumPix, mask, kernel, kernelSize, imageResult);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tconv);

        // post process
        double sqSum = 0.0;
        for (uint32_t i = 0; i < imageNumPix; i++) {
            float result = (float)imageResult[i];
            sqSum += result * result;
        }

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tend);

        // if using Gaussian kernel to blur, divide by kernel prefactor to
        // preserve flux
        if (kernel[4] == 4) {
            for (uint32_t i = 0; i < imageNumPix; i++) {
                imageResult[i] /= 16.0;
            }
        }

        printf("doConvolution took about %.7f seconds to do conv, %.7f seconds "
            "to calc sqSum %lf\n",
            ((double)tconv.tv_sec + 1.0e-9*tconv.tv_nsec) - 
            ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec),
            ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - 
            ((double)tconv.tv_sec + 1.0e-9*tconv.tv_nsec),
            sqSum);
    }

    // gaussian normalize
    // for (uint32_t i = 0; i < imageNumPix; i++) {
    //     imageResultDbl[i] = imageResult[i] / 16.0;
    // }

    saveArrayDumb(imageResult, imageWidth, imageNumPix);

    return 0;
}
