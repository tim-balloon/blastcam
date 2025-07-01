#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

float averageFloat(float* data, uint32_t n);
void doConvolution(
    float* imageBuffer,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    float* kernel,
    uint8_t kernelSize,
    float* imageResult);
