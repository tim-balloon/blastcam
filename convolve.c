#include "convolve.h"


/**
 * @brief first moment of an array
 * @param data pointer to array of length n
 * @param n length of data array and denominator in mean
 * @returns The truncated floating point mean
 */
uint16_t average(uint16_t* data, uint32_t n)
{
    float summed = 0.0;
    if (n < 1U) {
        return 0U;
    }
    for (uint32_t j = 1; j <= n;j ++ ) {
        summed += data[j];
    }
    return (uint16_t)truncf(summed / n);
}


/**
 * @brief Returns the 9 pixels including and surrounding the given pixel index.
 * 
 * @details Supplied arrays are accessed in C row-major order. In row-major 
 * order, left/right adjacent pixels are adjacent in the array, while top/
 * bottom pixels are `imageWidth` indices apart. For edge cases, return the
 * nearest valid neighbor.
 * @param[in] imageBuffer contiguous memory containing original pixel values
 * @param pixelIndex raw row-major index into image memory
 * @param imageWidth number of columns in a single image row
 * @param imageNumPix number of columns x number of rows in image
 * @param[out] neighborhood 9-element array containing 3x3 block of pixels around
 * `pixelIndex`
 */
void getNeighborhoodNearest(
    uint16_t* imageBuffer,
    uint32_t pixelIndex,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    uint16_t* neighborhood)
{
    // Clawing back a couple of instructions due to repeated ops
    int64_t above = pixelIndex + imageWidth;
    int64_t below = (int64_t)pixelIndex - imageWidth;

    bool isLeft = (0 == pixelIndex % imageWidth);
    bool isRight = (0 == ((pixelIndex + 1) % imageWidth));
    bool isBottom = below < 0;
    bool isTop = above >= imageNumPix;

    // DOWN is more negative
    // LEFT is more negative
    //  --------------  //  --------------
    // | 6  | 7  | 8  | // | TL | T  | TR |
    // |----|----|----| // |----|----|----|
    // | 3  | 4  | 5  | // | L  | C  | R  |
    // |----|----|----| // |----|----|----|
    // | 0  | 1  | 2  | // | BL | B  | BR |
    //  --------------  //  --------------
    int64_t idxTL = above - 1;
    int64_t idxT = above;
    int64_t idxTR = above + 1;
    int64_t idxL = pixelIndex - 1;
    int64_t idxC = pixelIndex;
    int64_t idxR = pixelIndex + 1;
    int64_t idxBL = below - 1;
    int64_t idxB = below;
    int64_t idxBR = below + 1;

    if (!isLeft && !isBottom && !isRight && !isTop) {
        // We're just normal men...we're just ordinary men...
        neighborhood[0] = imageBuffer[idxBL];
        neighborhood[1] = imageBuffer[idxB];
        neighborhood[2] = imageBuffer[idxBR];
        neighborhood[3] = imageBuffer[idxL];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxR];
        neighborhood[6] = imageBuffer[idxTL];
        neighborhood[7] = imageBuffer[idxT];
        neighborhood[8] = imageBuffer[idxTR];
        return;
    }
    // Less common cases: edge pixel
    if (isBottom && !isLeft && !isRight) {
        neighborhood[0] = imageBuffer[idxL];
        neighborhood[1] = imageBuffer[idxC];
        neighborhood[2] = imageBuffer[idxR];
        neighborhood[3] = imageBuffer[idxL];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxR];
        neighborhood[6] = imageBuffer[idxTL];
        neighborhood[7] = imageBuffer[idxT];
        neighborhood[8] = imageBuffer[idxTR];
        return;
    }
    if (isLeft && !isTop && !isBottom) {
        neighborhood[0] = imageBuffer[idxB];
        neighborhood[1] = imageBuffer[idxB];
        neighborhood[2] = imageBuffer[idxBR];
        neighborhood[3] = imageBuffer[idxC];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxR];
        neighborhood[6] = imageBuffer[idxT];
        neighborhood[7] = imageBuffer[idxT];
        neighborhood[8] = imageBuffer[idxTR];
        return;
    }
    if (isRight && !isTop && !isBottom) {
        neighborhood[0] = imageBuffer[idxBL];
        neighborhood[1] = imageBuffer[idxB];
        neighborhood[2] = imageBuffer[idxB];
        neighborhood[3] = imageBuffer[idxL];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxC];
        neighborhood[6] = imageBuffer[idxTL];
        neighborhood[7] = imageBuffer[idxT];
        neighborhood[8] = imageBuffer[idxT];
        return;
    }
    if (isTop && !isLeft && !isRight) {
        neighborhood[0] = imageBuffer[idxBL];
        neighborhood[1] = imageBuffer[idxB];
        neighborhood[2] = imageBuffer[idxBR];
        neighborhood[3] = imageBuffer[idxL];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxR];
        neighborhood[6] = imageBuffer[idxL];
        neighborhood[7] = imageBuffer[idxC];
        neighborhood[8] = imageBuffer[idxR];
        return;
    }
    // Least common cases: corner pixel
    if (isBottom && isLeft) {
        neighborhood[0] = imageBuffer[idxC];
        neighborhood[1] = imageBuffer[idxC];
        neighborhood[2] = imageBuffer[idxR];
        neighborhood[3] = imageBuffer[idxC];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxR];
        neighborhood[6] = imageBuffer[idxT];
        neighborhood[7] = imageBuffer[idxT];
        neighborhood[8] = imageBuffer[idxTR];
        return;
    }
    if (isBottom && isRight) {
        neighborhood[0] = imageBuffer[idxL];
        neighborhood[1] = imageBuffer[idxC];
        neighborhood[2] = imageBuffer[idxC];
        neighborhood[3] = imageBuffer[idxL];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxC];
        neighborhood[6] = imageBuffer[idxTL];
        neighborhood[7] = imageBuffer[idxT];
        neighborhood[8] = imageBuffer[idxT];
        return;
    }
    if (isTop && isLeft) {
        neighborhood[0] = imageBuffer[idxB];
        neighborhood[1] = imageBuffer[idxB];
        neighborhood[2] = imageBuffer[idxBR];
        neighborhood[3] = imageBuffer[idxC];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxR];
        neighborhood[6] = imageBuffer[idxC];
        neighborhood[7] = imageBuffer[idxC];
        neighborhood[8] = imageBuffer[idxR];
        return;
    }
    if (isTop && isRight) {
        neighborhood[0] = imageBuffer[idxBL];
        neighborhood[1] = imageBuffer[idxB];
        neighborhood[2] = imageBuffer[idxB];
        neighborhood[3] = imageBuffer[idxL];
        neighborhood[4] = imageBuffer[idxC];
        neighborhood[5] = imageBuffer[idxC];
        neighborhood[6] = imageBuffer[idxL];
        neighborhood[7] = imageBuffer[idxC];
        neighborhood[8] = imageBuffer[idxC];
        return;
    }
}


/**
 * @brief The innermost part of the convolution algorithm, add-and-multiply
 * pixels by kernel elements. Optimized for convolving 2 3x3 arrays
 * 
 * @param[in] array the input array to be considered; intended to be the output of
 * `getNeighborhood`
 * @param kernel the convolution kernel. Don't pass arrays of other types in
 * here, you heathen.
 * @return the result of the convolution
 */
float convolve9(uint16_t* array, int16_t* kernel)
{
    return (
        (float)array[0] * (float)kernel[8] +
        (float)array[1] * (float)kernel[7] +
        (float)array[2] * (float)kernel[6] +
        (float)array[3] * (float)kernel[5] +
        (float)array[4] * (float)kernel[4] +
        (float)array[5] * (float)kernel[3] +
        (float)array[6] * (float)kernel[2] +
        (float)array[7] * (float)kernel[1] +
        (float)array[8] * (float)kernel[0]
    );
}


/**
 * @brief Implements a limited, 3x3 convolution over the image.
 * 
 * @details Proceeds over the contiguous image buffer pixel-by-pixel,
 * convolving the 3x3 neighborhood of each pixel with the supplied 3x3 kernel.
 * This version is intended for integer-valued kernels, i.e., boxcar, Sobel 
 * filtering, or even Gaussian filtering before dividing by the normalization.
 * The primary application will be autofocusing. For filtering large-scale
 * noise like PMCs, a variable-radius filter is required.
 * 
 * @param[in] imageBuffer
 * @param imageMean
 * @param imageWidth 
 * @param imageNumPix 
 * @param[in] mask hot pixel mask, whether or not to include pixel in calculations
 * @param[in] kernel 
 * @param kernelSize 
 * @param[out] imageResult 
 */
void doConvolution(
    uint16_t* imageBuffer,
    uint16_t imageMean,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    int16_t* kernel,
    uint8_t kernelSize,
    float* imageResult)
{
    // statically allocate this array for (re)use throughout the run
    uint16_t neighborhood[9] = {0};
    for (uint32_t i = 0; i < imageNumPix; i++) {
        getNeighborhoodNearest(imageBuffer, i, imageWidth, imageNumPix, neighborhood);
        imageResult[i] = convolve9(neighborhood, kernel) * (float)(mask[i]);
    }
}