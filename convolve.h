#ifndef _CONVOLVE_H
#define _CONVOLVE_H
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_CONVOLVE_SIDE_LEN 15

int imageStats(
    float* data,
    uint32_t n,
    float* pAverage,
    float* pMax,
    uint32_t* pIdxMax);
void readROI(
    float* pImageBuffer,
    uint8_t* pMask,
    uint32_t ROIcenter,
    uint8_t ROIsideLength,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pROIbuffer,
    uint32_t* pROInumPixRead,
    uint8_t* pROIsideLengthRead);
void doConvolution3x3(
    float* pImageBuffer,
    uint8_t* pMask,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pKernel,
    float* pImageResult);
void doConvolutionNxN(
    float* pImageBuffer,
    uint8_t* pMask,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pKernel,
    uint8_t kernelSideLength,
    float* pImageResult);
void doBoxcarNxN(
    float* pImageBuffer,
    uint8_t* pMask,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    uint8_t radius,
    float* pIntermediate,
    float* pImageResult);

#endif