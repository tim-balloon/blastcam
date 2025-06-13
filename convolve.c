#include "convolve.h"


/**
 * @brief first moment of an array
 * @param data pointer to array of length n
 * @param n length of data array and denominator in mean
 * @returns The truncated floating point mean
 */
uint16_t average(uint16_t* data, uint32_t n) {
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
 * bottom pixels are `imageWidth` indices apart. For edge cases, place 0s in
 * `neighborhood`, to zero out that term in convolution.
 * @param[in] imageBuffer contiguous memory containing original pixel values
 * @param imageMean mean value of image, for proper handling of edges
 * @param pixelIndex raw row-major index into image memory
 * @param imageWidth number of columns in a single image row
 * @param imageNumPix number of columns x number of rows in image
 * @param[out] neighborhood 9-element array containing 3x3 block of pixels around
 * `pixelIndex`
 */
void getNeighborhood(
    int32_t* imageBuffer,
    int32_t imageMean,
    uint32_t pixelIndex,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    int32_t* neighborhood) {
    bool isLeft = (0 == pixelIndex % imageWidth);
    bool isRight = (0 == ((pixelIndex + 1) % imageWidth));
    bool isBottom = ((int64_t)pixelIndex - imageWidth) < 0;
    bool isTop = (pixelIndex + imageWidth) >= imageNumPix;

    // HACK: FIXME: sobelmetric returns to 0 when high?
    imageMean = 0U;

    // If not blocked by image bound guards, return pixel value, else return image mean value
    neighborhood[0] = (!isBottom && !isLeft)     ? imageBuffer[pixelIndex - imageWidth - 1] : imageMean;
    neighborhood[1] = (!isBottom)                ? imageBuffer[pixelIndex - imageWidth]     : imageMean;
    neighborhood[2] = (!isBottom && !isRight)    ? imageBuffer[pixelIndex - imageWidth + 1] : imageMean;
    neighborhood[3] = (!isLeft)                  ? imageBuffer[pixelIndex - 1]              : imageMean;
    // ought to be limited by caller's for loop, but belt & suspenders
    neighborhood[4] = (pixelIndex < imageNumPix) ? imageBuffer[pixelIndex]                  : imageMean;
    neighborhood[5] = (!isRight)                 ? imageBuffer[pixelIndex + 1]              : imageMean;
    neighborhood[6] = (!isTop && !isLeft)        ? imageBuffer[pixelIndex + imageWidth - 1] : imageMean;
    neighborhood[7] = (!isTop)                   ? imageBuffer[pixelIndex + imageWidth]     : imageMean;
    neighborhood[8] = (!isTop && !isRight)       ? imageBuffer[pixelIndex + imageWidth + 1] : imageMean;
}


/**
 * @brief The innermost part of the convolution algorithm, add-and-multiply
 * pixels by kernel elements.
 * 
 * @param[in] array the input array to be considered; intended to be the output of
 * `getNeighborhood`
 * @param kernel the convolution kernel. Don't pass arrays of other types in
 * here, you heathen.
 * @param kernelSize the length of the flattened, square kernel
 * @return int32_t the result of the convolution
 */
int32_t convolve(int32_t* array, int16_t* kernel, uint8_t kernelSize) {
    int32_t sum = 0;
    // Assumption: array and kernel are same size.
    for (uint8_t i = 0; i < kernelSize; i++) {
        sum += array[i] * (int32_t)kernel[kernelSize - i - 1];
    }
    return sum;
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
    int32_t* imageBuffer,
    int32_t imageMean,
    uint16_t imageWidth,
    uint32_t imageNumPix,
    unsigned char* mask,
    int16_t* kernel,
    uint8_t kernelSize,
    int32_t* imageResult) {
    // statically allocate this array for (re)use throughout the run
    int32_t neighborhood[9] = {0};
    for (uint32_t i = 0; i < imageNumPix; i++) {
        getNeighborhood(imageBuffer, imageMean, i, imageWidth, imageNumPix, neighborhood);
        imageResult[i] = convolve(neighborhood, kernel, kernelSize) * (int32_t)(mask[i]);
    }
}