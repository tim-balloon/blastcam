#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

uint16_t average(uint16_t* data, uint32_t n);
void getNeighborhood(
    uint16_t* imageBuffer,
    uint16_t imageMean,
    uint32_t pixelIndex,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    int32_t* neighborhood);
float convolve(uint16_t* array, int16_t* kernel, uint8_t kernelSize);
void doConvolution(
    uint16_t* imageBuffer,
    uint16_t imageMean,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    int16_t* kernel,
    uint8_t kernelSize,
    float* imageResult);
