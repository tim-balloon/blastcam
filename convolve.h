#ifndef _CONVOLVE_H
#define _CONVOLVE_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>


int imageStats(
    float* data,
    uint32_t n,
    float* pAverage,
    float* pMax,
    uint32_t* pIdxMax);
int readROI(
    float* pImageBuffer,
    uint32_t ROIcenter,
    uint8_t ROIsideLength,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pROIbuffer,
    uint32_t* pROInumPixRead,
    uint8_t* pROIsideLengthRead);
void doConvolution(
    float* imageBuffer,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    float* kernel,
    uint8_t kernelSize,
    float* imageResult);

#endif