#include <stdbool.h>
#include <stdint.h>

void getNeighborhood(
    uint16_t* imageBuffer,
    uint32_t pixelIndex,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    uint16_t* neighborhood);
int32_t convolve(uint16_t* array, int16_t* kernel, uint8_t kernelSize);
void doConvolution(
    uint16_t* imageBuffer,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    int16_t* kernel,
    uint8_t kernelSize,
    int32_t* imageResult);
