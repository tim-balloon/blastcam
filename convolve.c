#include "convolve.h"


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
    for (uint32_t ii = 1; ii <= n; ii++) {
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
 * @brief Returns the flattened array indices of the top-left and bottom-right
 * corners of a region of interest (ROI) centered on `idx` with side length
 * `sideLength`.
 * @details For even sideLength, the ROI returned is `sideLength` + 1. The ROI
 * is limited to the top, bottom, left, and right edges.
 * 
 * @param idx flattened array index of ROI center
 * @param imageWidth 
 * @param imageNumPix total number of image pixels 
 * @param sideLength total side length of ROI
 * @param[out] pIdxTopLeft flattened array index of top-left ROI corner
 * @param[out] pIdxBottomRight flattened array index of bottom-right ROI corner
 * @return int -1 if failed, 0 otherwise
 */
int calcROIcorners(
    uint32_t idx,
    uint8_t sideLength,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    uint32_t* pIdxTopLeft,
    uint32_t* pIdxBottomRight)
{
    uint8_t numExtraRows = 0U;
    uint8_t halfSideLength = sideLength / 2U;

    // Limit left/right to available space
    uint16_t availLeft = idx % imageWidth;
    uint16_t availRight = imageWidth - availLeft - 1U;

    availLeft = (availLeft > halfSideLength) ? halfSideLength : availLeft;
    availRight = (availRight > halfSideLength) ? halfSideLength : availRight;

    int64_t possibleTopLeft = (int64_t)idx - imageWidth * halfSideLength - availLeft;
    int64_t possibleBottomRight = idx + imageWidth * halfSideLength + availRight;

    if (possibleBottomRight >= imageNumPix) {
        // number of rows past bottom
        numExtraRows = ((possibleBottomRight - imageNumPix) / imageWidth) + 1;
        // limit the bottom-right corner to the bottom row
        *pIdxBottomRight = possibleBottomRight - (numExtraRows * imageWidth);
    } else {
        *pIdxBottomRight = (uint32_t)possibleBottomRight;
    }

    if (possibleTopLeft < 0) {
        // number of rows above top
        numExtraRows = possibleTopLeft / imageWidth;
        // limit the top-left corner to the top row
        *pIdxTopLeft = possibleTopLeft - (numExtraRows * imageWidth);
    } else {
        *pIdxTopLeft = (uint32_t)possibleTopLeft;
    }
    return 0;
}


/**
 * @brief Reads all available pixels of the ROI defined by calcROIcorners into
 * the buffer ROIbuffer.
 * @details pROIbuffer should be allocated large enough to handle
 * (ROIsideLength + (1 - ROIsideLength % 2))^2 values. Not all spaces will be
 * filled, if the ROI is limited by an edge.
 * pROInumPixRead and pROIsideLengthRead are filled with the actual number of
 * ROI pixels read, and the actual ROI width, for the caller to use when reading
 * the contents of the ROI.
 * 
 * @param[in] pImageBuffer overall image from which the ROI is read, flattened, row-
 * major order
 * @param ROIcenter center location index in the flattened image imageBuffer
 * @param ROIsideLength requested ROI side length. Even values are promoted to
 * the next odd value to keep the ROI centered on ROIcenter.
 * @param imageWidth total width of image pointed to by imageBuffer, in pixels
 * @param imageNumPix total length of imageBuffer
 * @param[out] pROIbuffer array large enough to hold at least
 * (ROIsideLength + (1 - ROIsideLength % 2))^2 values
 * @param[out] pROInumPixRead the actual number of pixels read from the ROI. Use this
 * to safely read the returned ROIbuffer.
 * @param[out] pROIsideLengthRead the actual number side length of the ROI. Use
 * this to safely read the returned ROIbuffer.
 * @return -1 if failed, 0 otherwise 
 */
int readROI(
    float* pImageBuffer,
    uint32_t ROIcenter,
    uint8_t ROIsideLength,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pROIbuffer,
    uint32_t* pROInumPixRead,
    uint8_t* pROIsideLengthRead)
{
    int ret = 0;
    uint32_t idxTopLeft = 0;
    uint32_t idxBottomRight = imageNumPix;

    // Get the start/stop indices within the larger array
    ret = calcROIcorners(ROIcenter, ROIsideLength, imageWidth, imageNumPix, &idxTopLeft, &idxBottomRight);

    uint16_t colTopLeft = idxTopLeft % imageWidth;
    uint16_t colBottomRight = idxBottomRight % imageWidth;
    uint32_t bufferIdx = idxTopLeft;
    // The current index within the ROI buffer
    uint32_t ROIidx = 0;
    // Never read outside the input buffer and stop after reaching the end of
    // the ROI
    while ((bufferIdx < imageNumPix) && (bufferIdx <= idxBottomRight)) {
        pROIbuffer[ROIidx] = pImageBuffer[bufferIdx];
        // Determine when to skip to the next ROI row start
        if ((bufferIdx % imageWidth) == colBottomRight) {
            // next index is same col as top left corner, and one row greater
            // than current row:
            // (bufferIdx / imageWidth + 1) is one row greater than current,
            // (bufferIdx / imageWidth + 1) * imageWidth is the index of the
            //   beginning of that row,
            // colTopLeft is the offset into that row.
            bufferIdx = colTopLeft + (bufferIdx / imageWidth + 1) * imageWidth;
        } else {
            bufferIdx++;
        }
        ROIidx++;
    }
    *pROInumPixRead = ROIidx;
    *pROIsideLengthRead = (colBottomRight - colTopLeft >= 0) ? colBottomRight - colTopLeft + 1 : 0U;
    return 0;
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
    float* imageBuffer,
    uint32_t pixelIndex,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* neighborhood)
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
float convolve9(float* array, float* kernel)
{
    return (
        array[0] * kernel[8] +
        array[1] * kernel[7] +
        array[2] * kernel[6] +
        array[3] * kernel[5] +
        array[4] * kernel[4] +
        array[5] * kernel[3] +
        array[6] * kernel[2] +
        array[7] * kernel[1] +
        array[8] * kernel[0]
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
 * @param imageWidth 
 * @param imageNumPix 
 * @param[in] mask hot pixel mask, whether or not to include pixel in calculations
 * @param[in] kernel 
 * @param kernelSize 
 * @param[out] imageResult 
 */
void doConvolution(
    float* imageBuffer,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    float* kernel,
    uint8_t kernelSize,
    float* imageResult)
{
    // statically allocate this array for (re)use throughout the run
    float neighborhood[9] = {0.0};
    for (uint32_t i = 0; i < imageNumPix; i++) {
        getNeighborhoodNearest(imageBuffer, i, imageWidth, imageNumPix, neighborhood);
        imageResult[i] = convolve9(neighborhood, kernel) * (float)(mask[i]);
    }
}