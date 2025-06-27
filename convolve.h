#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

uint16_t average(uint16_t* data, uint32_t n);
void doConvolution(
    uint16_t* imageBuffer,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    float* kernel,
    uint8_t kernelSize,
    float* imageResult);
