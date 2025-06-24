#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

uint16_t average(uint16_t* data, uint32_t n);
void doConvolution(
    uint16_t* imageBuffer,
    uint16_t imageMean,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    int16_t* kernel,
    uint8_t kernelSize,
    float* imageResult);
