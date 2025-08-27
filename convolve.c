#include "convolve.h"
#include "stdio.h"
#include "stdlib.h"

/**
 * @brief calculates various stats that require looping over the whole image
 * 
 * @param data pointer to array of length n
 * @param n length of data array and denominator in mean
 * @param pAverage average value
 * @param pMax max value
 * @param pIdxMax index of max value
 * @returns -1 on failure, 0 otherwise
 */
int imageStats(float* data, uint32_t n, float* pAverage, float* pMax,
    uint32_t* pIdxMax)
{
    // ASSUMPTION: input values are limited to plausible ADU values, i.e. less
    // than 2^16 - 1, to avoid overflow of float during sum
    float summed = 0.0;
    float max = -1.0;
    uint32_t idxMax = 0;
    if (n < 1U) {
        return -1;
    }
    for (uint32_t ii = 0; ii < n; ii++) {
        float thisData = data[ii];
        summed += thisData;
        if (thisData >= max) {
            max = thisData;
            idxMax = ii;
        }
    }
    *pAverage = summed / n;
    *pMax = max;
    *pIdxMax = idxMax;
    return 0;
}


/**
 * @brief Returns the 9 pixels including and surrounding the given pixel index.
 * 
 * @details Supplied arrays are accessed in C row-major order. In row-major 
 * order, left/right adjacent pixels are adjacent in the array, while top/
 * bottom pixels are `imageWidth` indices apart. For edge cases, return the
 * nearest valid neighbor.
 * @param[in] pImageBuffer contiguous memory containing original pixel values
 * @param pixelIndex raw row-major index into image memory
 * @param imageWidth number of columns in a single image row
 * @param imageNumPix number of columns x number of rows in image
 * @param[out] pNeighborhood 9-element array containing 3x3 block of pixels around
 * `pixelIndex`
 */
void getNeighborhood3x3(
    float* pImageBuffer,
    uint32_t pixelIndex,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pNeighborhood)
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
        pNeighborhood[0] = pImageBuffer[idxBL];
        pNeighborhood[1] = pImageBuffer[idxB];
        pNeighborhood[2] = pImageBuffer[idxBR];
        pNeighborhood[3] = pImageBuffer[idxL];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxR];
        pNeighborhood[6] = pImageBuffer[idxTL];
        pNeighborhood[7] = pImageBuffer[idxT];
        pNeighborhood[8] = pImageBuffer[idxTR];
        return;
    }
    // Less common cases: edge pixel
    if (isBottom && !isLeft && !isRight) {
        pNeighborhood[0] = pImageBuffer[idxL];
        pNeighborhood[1] = pImageBuffer[idxC];
        pNeighborhood[2] = pImageBuffer[idxR];
        pNeighborhood[3] = pImageBuffer[idxL];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxR];
        pNeighborhood[6] = pImageBuffer[idxTL];
        pNeighborhood[7] = pImageBuffer[idxT];
        pNeighborhood[8] = pImageBuffer[idxTR];
        return;
    }
    if (isLeft && !isTop && !isBottom) {
        pNeighborhood[0] = pImageBuffer[idxB];
        pNeighborhood[1] = pImageBuffer[idxB];
        pNeighborhood[2] = pImageBuffer[idxBR];
        pNeighborhood[3] = pImageBuffer[idxC];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxR];
        pNeighborhood[6] = pImageBuffer[idxT];
        pNeighborhood[7] = pImageBuffer[idxT];
        pNeighborhood[8] = pImageBuffer[idxTR];
        return;
    }
    if (isRight && !isTop && !isBottom) {
        pNeighborhood[0] = pImageBuffer[idxBL];
        pNeighborhood[1] = pImageBuffer[idxB];
        pNeighborhood[2] = pImageBuffer[idxB];
        pNeighborhood[3] = pImageBuffer[idxL];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxC];
        pNeighborhood[6] = pImageBuffer[idxTL];
        pNeighborhood[7] = pImageBuffer[idxT];
        pNeighborhood[8] = pImageBuffer[idxT];
        return;
    }
    if (isTop && !isLeft && !isRight) {
        pNeighborhood[0] = pImageBuffer[idxBL];
        pNeighborhood[1] = pImageBuffer[idxB];
        pNeighborhood[2] = pImageBuffer[idxBR];
        pNeighborhood[3] = pImageBuffer[idxL];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxR];
        pNeighborhood[6] = pImageBuffer[idxL];
        pNeighborhood[7] = pImageBuffer[idxC];
        pNeighborhood[8] = pImageBuffer[idxR];
        return;
    }
    // Least common cases: corner pixel
    if (isBottom && isLeft) {
        pNeighborhood[0] = pImageBuffer[idxC];
        pNeighborhood[1] = pImageBuffer[idxC];
        pNeighborhood[2] = pImageBuffer[idxR];
        pNeighborhood[3] = pImageBuffer[idxC];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxR];
        pNeighborhood[6] = pImageBuffer[idxT];
        pNeighborhood[7] = pImageBuffer[idxT];
        pNeighborhood[8] = pImageBuffer[idxTR];
        return;
    }
    if (isBottom && isRight) {
        pNeighborhood[0] = pImageBuffer[idxL];
        pNeighborhood[1] = pImageBuffer[idxC];
        pNeighborhood[2] = pImageBuffer[idxC];
        pNeighborhood[3] = pImageBuffer[idxL];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxC];
        pNeighborhood[6] = pImageBuffer[idxTL];
        pNeighborhood[7] = pImageBuffer[idxT];
        pNeighborhood[8] = pImageBuffer[idxT];
        return;
    }
    if (isTop && isLeft) {
        pNeighborhood[0] = pImageBuffer[idxB];
        pNeighborhood[1] = pImageBuffer[idxB];
        pNeighborhood[2] = pImageBuffer[idxBR];
        pNeighborhood[3] = pImageBuffer[idxC];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxR];
        pNeighborhood[6] = pImageBuffer[idxC];
        pNeighborhood[7] = pImageBuffer[idxC];
        pNeighborhood[8] = pImageBuffer[idxR];
        return;
    }
    if (isTop && isRight) {
        pNeighborhood[0] = pImageBuffer[idxBL];
        pNeighborhood[1] = pImageBuffer[idxB];
        pNeighborhood[2] = pImageBuffer[idxB];
        pNeighborhood[3] = pImageBuffer[idxL];
        pNeighborhood[4] = pImageBuffer[idxC];
        pNeighborhood[5] = pImageBuffer[idxC];
        pNeighborhood[6] = pImageBuffer[idxL];
        pNeighborhood[7] = pImageBuffer[idxC];
        pNeighborhood[8] = pImageBuffer[idxC];
        return;
    }
}


/**
 * @brief The innermost part of the convolution algorithm, add-and-multiply
 * pixels by kernel elements. Optimized for convolving 2 3x3 arrays
 * 
 * @param[in] array the input array to be considered; intended to be the output of
 * `getNeighborhood`
 * @param pKernel the convolution kernel. Don't pass arrays of other types in
 * here, you heathen.
 * @return the result of the convolution
 */
float convolve9(float* pArray, float* pKernel)
{
    return (
        pArray[0] * pKernel[8] +
        pArray[1] * pKernel[7] +
        pArray[2] * pKernel[6] +
        pArray[3] * pKernel[5] +
        pArray[4] * pKernel[4] +
        pArray[5] * pKernel[3] +
        pArray[6] * pKernel[2] +
        pArray[7] * pKernel[1] +
        pArray[8] * pKernel[0]
    );
}


/**
 * @brief Implements a limited, 3x3 convolution over the image.
 * 
 * @details Proceeds over the contiguous image buffer pixel-by-pixel,
 * convolving the 3x3 neighborhood of each pixel with the supplied 3x3 kernel.
 * For filtering large-scale noise like PMCs, a variable-radius filter is
 * required.
 * 
 * @param[in] pImageBuffer
 * @param imageWidth 
 * @param imageNumPix 
 * @param[in] pMask hot pixel mask, whether or not to include pixel in calculations
 * @param[in] pKernel 
 * @param[out] pImageResult 
 */
void doConvolution3x3(
    float* pImageBuffer,
    uint8_t* pMask,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pKernel,
    float* pImageResult)
{
    // statically allocate this array for (re)use throughout the run
    float pNeighborhood[9] = {0.0};
    for (uint32_t i = 0; i < imageNumPix; i++) {
        getNeighborhood3x3(pImageBuffer, i, imageWidth, imageNumPix, pNeighborhood);
        pImageResult[i] = convolve9(pNeighborhood, pKernel) * (float)(pMask[i]);
    }
}