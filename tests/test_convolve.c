#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../convolve.h"


int saveArrayDumb(double* imageResult, uint16_t imageWidth, uint32_t imageNumPix) {
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
        fprintf(fp, "%f", imageResult[i]);
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
    uint8_t imageWidth = 11;
    uint8_t imageHeight = 11;
    uint32_t imageNumPix = imageWidth * imageHeight;
    uint16_t imageBuffer[121] = {0};
    imageBuffer[imageNumPix / 2] = 100;

    // uint16_t kernel[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1}; // gaussian
    // technically, sobel takes G = (G_x^2 + G_y^2)^.5 as an approximation of
    // the gradient, but stars are round, so any stars (or indeed other
    // features) will be sharp in x,y, or any other direction.
    uint16_t kernel[9] = {-1, 0, 1, -2, 0, 2, -1, 0, 1}; // sobel x
    uint16_t kernelSize = 9;

    int32_t imageResult[121] = {0};
    double imageResultDbl[121] = {0.0};

    struct timespec tstart = {0,0};
    struct timespec tend = {0,0};

    int nCalls = 100;
    while (nCalls > 0) {
        nCalls -= 1;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tstart);

        doConvolution(imageBuffer, imageWidth, imageNumPix, kernel, kernelSize, imageResult);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tend);
        printf("doConvolution took about %.7f seconds\n",
            ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - 
            ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec));
    }

    // gaussian normalize
    // for (uint32_t i = 0; i < imageNumPix; i++) {
    //     imageResultDbl[i] = imageResult[i] / 16.0;
    // }
    for (uint32_t i = 0; i < imageNumPix; i++) {
        imageResultDbl[i] = imageResult[i] * 1.0;
    }

    saveArrayDumb(imageResultDbl, imageWidth, imageNumPix);

    return 0;
}
