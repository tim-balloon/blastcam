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
 */
void calcROIcorners(
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
 * @param[in] pMask overall image mask, full of 1s or 0s for dealing with hot
 * pixels
 * @param ROIcenter center location index in the flattened image pImageBuffer
 * @param ROIsideLength requested ROI side length. Even values are promoted to
 * the next odd value to keep the ROI centered on ROIcenter.
 * @param imageWidth total width of image pointed to by pImageBuffer, in pixels
 * @param imageNumPix total length of pImageBuffer
 * @param[out] pROIbuffer array large enough to hold at least
 * (ROIsideLength + (1 - ROIsideLength % 2))^2 values
 * @param[out] pROInumPixRead the actual number of pixels read from the ROI. Use this
 * to safely read the returned ROIbuffer.
 * @param[out] pROIsideLengthRead the actual side length of the ROI. Use
 * this to safely read the returned ROIbuffer.

 */
void readROI(
    float* pImageBuffer,
    uint8_t* pMask,
    uint32_t ROIcenter,
    uint8_t ROIsideLength,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pROIbuffer,
    uint32_t* pROInumPixRead,
    uint8_t* pROIsideLengthRead)
{
    uint32_t idxTopLeft = 0;
    uint32_t idxBottomRight = imageNumPix;

    // Get the start/stop indices within the larger array
    calcROIcorners(ROIcenter, ROIsideLength, imageWidth, imageNumPix, &idxTopLeft, &idxBottomRight);

    uint16_t colTopLeft = idxTopLeft % imageWidth;
    uint16_t colBottomRight = idxBottomRight % imageWidth;
    uint32_t bufferIdx = idxTopLeft;
    // The current index within the ROI buffer
    uint32_t ROIidx = 0;
    // Never read outside the input buffer and stop after reaching the end of
    // the ROI
    while ((bufferIdx < imageNumPix) && (bufferIdx <= idxBottomRight)) {
        pROIbuffer[ROIidx] = pImageBuffer[bufferIdx] * pMask[bufferIdx];
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


/**
 * @brief Implements an NxN convolution over the image, where N is an odd
 * number <= MAX_CONVOLVE_SIDE_LEN. N > MAX_CONVOLVE_SIDE_LEN will be limited to
 * MAX_CONVOLVE_SIDE_LEN (15 by default).
 * 
 * @details Proceeds over the contiguous image buffer pixel-by-pixel,
 * convolving the NxN neighborhood of each pixel with the supplied NxN kernel.
 * A border of pixels N // 2 (integer division) will be identical to the 
 * original contents of imageResult. The closest scipy/astropy equivalent is
 * astropy.convolve(boundary=None), but instead of setting the edges to 0, they
 * are unchanged from whatever imageResult originally contained. This avoids a
 * fair bit of complicated and expensive edge handling, but it could be
 * improved.
 * 
 * So, if you'd like the border to be all zeros, pass a zero-filled array, or if
 * you'd like the result unchanged from the original image, memcpy it into
 * imageResult first.
 * 
 * @note If filtering an image in order to implement an unsharp mask (high-pass
 * filter), pass in a 0s pImageResult. Subtracting the result from the original
 * image will leave the border unchanged from the original image. If you memcpy
 * first, the border of the unsharp masked image will be original - original = 0
 * 
 * @param[in] pImageBuffer flattened image array
 * @param[in] pMask flattened mask array for dealing with hot pixels
 * @param imageWidth 
 * @param imageNumPix 
 * @param[in] pMask hot pixel mask, whether or not to include pixel in calculations
 * @param[in] pKernel array of length N times N, N < 16
 * @param[in] kernelSideLength N
 * @param[out] pImageResult
 */
void doConvolutionNxN(
    float* pImageBuffer,
    uint8_t* pMask,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    float* pKernel,
    uint8_t kernelSideLength,
    float* pImageResult)
{
    // allocate this array for (re)use throughout the run. Arrays
    // smaller than MAX_CONVOLVE_SIDE_LEN will simply not need the trailing
    // elements.
    float pNeighborhood[MAX_CONVOLVE_SIDE_LEN * MAX_CONVOLVE_SIDE_LEN] = {0.0};
    // Limit the used kernel to a sane size for convolution.
    // "Sane size" depends on your image size and tolerance for latency, mostly.
    if (kernelSideLength > MAX_CONVOLVE_SIDE_LEN) {
        kernelSideLength = MAX_CONVOLVE_SIDE_LEN;
    }
    if (kernelSideLength > imageWidth) {
        kernelSideLength = imageWidth;
    }
    // Calculate the border size required for this kernel. Note integer division
    int8_t borderSize = kernelSideLength / 2;
    uint16_t kernelNumPix = kernelSideLength * kernelSideLength;

    // Calculate the start/stop indices in the overall array, given this border
    // size.
    uint32_t startIdx = (imageWidth * borderSize) + borderSize;
    uint32_t stopIdx = imageNumPix - startIdx;
    uint32_t ii = startIdx;
    while (ii < stopIdx) {
        uint32_t pROInumPixRead = 0U;
        uint8_t pROIsideLengthRead = 0U;
        // Read a NxN block centered on ii into the allocated buffer
        readROI(pImageBuffer, pMask, ii, kernelSideLength, imageWidth, 
            imageNumPix, pNeighborhood, &pROInumPixRead, &pROIsideLengthRead);

        // Direct kernel convolution
        float convolution = 0.0;
        for (uint8_t jj = 0; jj < pROInumPixRead; jj++) {
            // pNeighborhood is masked in readROI()
            convolution += pNeighborhood[jj] * pKernel[kernelNumPix - 1 - jj];
        }
        pImageResult[ii] = convolution;

        // After reaching the border, skip to the next row.
        if (0 == ((ii + borderSize + 1) % imageWidth)) {
            // new index is same col as start index, and one row greater
            // than current row:
            // ((i / imageWidth)) is current row
            // ((i / imageWidth) + 1) is one row greater than current,
            // ((i / imageWidth) + 1) * imageWidth is the index of the
            // beginning of that row, borderSize is the offset into that row.
            ii = (ii / imageWidth + 1) * imageWidth + borderSize;
        } else {
            ii++;
        }
    }
}

/**
 * @brief Simple boxcar filter, separated in vertical/horizontal directions.
 * Does not preserve flux in a region radius from the edge.
 * 
 * @param pImageBuffer 
 * @param pMask 
 * @param imageWidth 
 * @param imageNumPix 
 * @param radius 
 * @param pIntermediate supply a buffer to use as temporary storage
 * @param pImageResult 
 */
void doBoxcarNxN(
    float* pImageBuffer,
    uint8_t* pMask,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    uint8_t radius,
    float* pIntermediate,
    float* pImageResult)
{
    uint16_t imageHeight = imageNumPix / imageWidth;
    float kernelNorm = (float)(2 * radius + 1);
    kernelNorm *= kernelNorm;
    // Separable filter: rows first, then columns
    // row-wise boxcar
    uint16_t stopRow = imageHeight - radius - 1;
    uint16_t stopCol = imageWidth - radius - 1;
    for (uint16_t row = radius; row < stopRow; row++) {
        for (uint16_t col = radius; col < stopCol; col++) {
            uint32_t idx = row * imageWidth + col;
            float avg = 0.0;
            for (int16_t ri = -radius; ri <= radius; ri++) {
                avg += pImageBuffer[idx + ri] * pMask[idx + ri];
            }
            pIntermediate[idx] = avg;
        }
    }
    // col-wise boxcar
    for (uint16_t col = radius; col < stopCol; col++) {
        for (uint16_t row = radius; row < stopRow; row++) {
            uint32_t idx = row * imageWidth + col;
            float avg = 0.0;
            for (int16_t ri = -radius; ri <= radius; ri++) {
                avg += pIntermediate[idx + ri * imageWidth] * pMask[idx + ri * imageWidth];
            }
            pImageResult[idx] = avg / kernelNorm;
        }
    }
}