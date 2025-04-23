#include <stdbool.h>
#include <stdint.h>

void getNeighborhood(
    int32_t* imageBuffer,
    uint32_t pixelIndex,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    int32_t* neighborhood);
int32_t convolve(int32_t* array, int16_t* kernel, uint8_t kernelSize);
void doConvolution(
    int32_t* imageBuffer,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    int16_t* kernel,
    uint8_t kernelSize,
    int32_t* imageResult);
