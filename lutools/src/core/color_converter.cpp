#include "sony2fuji/color_converter.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <limits>
#include <vector>

#ifdef _OPENMP
namespace {
constexpr size_t kParallelThreshold = 1u << 16;
}
#endif

namespace sony2fuji {

// ============================================================================
// ColorConverter Implementation
// ============================================================================

RGB ColorConverter::convert(const RGB& input, ColorSpace from, ColorSpace to) const {
    if (from == to) {
        return input;
    }

    Matrix3x3 matrix = getConversionMatrix(from, to);
    return applyMatrix(input, matrix);
}

ErrorCode ColorConverter::convertImage(ImageData& image, ColorSpace from, ColorSpace to) const {
    if (image.pixels.empty()) {
        return ErrorCode::ProcessingError;
    }

    if (from == to) {
        return ErrorCode::Success;
    }

    Matrix3x3 matrix = getConversionMatrix(from, to);

    const size_t pixel_count = image.pixels.size();
#ifdef _OPENMP
#pragma omp parallel for if (pixel_count >= kParallelThreshold)
#endif
    for (size_t i = 0; i < pixel_count; ++i) {
        image.pixels[i] = applyMatrix(image.pixels[i], matrix);
    }

    return ErrorCode::Success;
}

ErrorCode ColorConverter::convertImage(
    ImageData& image,
    const Matrix3x3& fromToXYZ,
    ColorSpace fromWhitePoint,
    ColorSpace to
) const {
    if (image.pixels.empty()) {
        return ErrorCode::ProcessingError;
    }

    Matrix3x3 matrix = getConversionMatrix(fromToXYZ, fromWhitePoint, to);

    const size_t pixel_count = image.pixels.size();
#ifdef _OPENMP
#pragma omp parallel for if (pixel_count >= kParallelThreshold)
#endif
    for (size_t i = 0; i < pixel_count; ++i) {
        image.pixels[i] = applyMatrix(image.pixels[i], matrix);
    }

    return ErrorCode::Success;
}

ColorConverter::Matrix3x3 ColorConverter::getConversionMatrix(ColorSpace from, ColorSpace to) {
    // 通过 XYZ 色彩空间作为中间媒介进行转换
    // from -> XYZ -> to

    Matrix3x3 fromToXYZ;
    Matrix3x3 xyzToTarget;
    Matrix3x3 adaptation;

    // 获取 from -> XYZ 矩阵
    switch (from) {
        case ColorSpace::SonyNative:
            fromToXYZ = getSonyNativeToXYZ();
            break;
        case ColorSpace::FujiFilm_FGamut:
            fromToXYZ = getFGamutToXYZ();
            break;
        case ColorSpace::ACES2065_1:
            fromToXYZ = getACES2065ToXYZ();
            break;
        case ColorSpace::ACEScg:
            fromToXYZ = getACEScgToXYZ();
            break;
        case ColorSpace::BT709:
            fromToXYZ = getBT709ToXYZ();
            break;
        case ColorSpace::ProPhotoRGB:
            fromToXYZ = getProPhotoToXYZ();
            break;
        case ColorSpace::AdobeRGB:
            fromToXYZ = getAdobeRGBToXYZ();
            break;
        case ColorSpace::sRGB:
            fromToXYZ = getSRGBToXYZ();
            break;
    }

    // 获取 XYZ -> to 矩阵
    xyzToTarget = getXYZToColorSpace(to);
    adaptation = getChromaticAdaptationMatrix(from, to);

    // 组合矩阵: from -> XYZ -> to
    return multiplyMatrices(xyzToTarget, multiplyMatrices(adaptation, fromToXYZ));
}

ColorConverter::Matrix3x3 ColorConverter::getConversionMatrix(
    const Matrix3x3& fromToXYZ,
    ColorSpace fromWhitePoint,
    ColorSpace to
) {
    Matrix3x3 xyzToTarget = getXYZToColorSpace(to);
    Matrix3x3 adaptation = getChromaticAdaptationMatrix(fromWhitePoint, to);

    return multiplyMatrices(xyzToTarget, multiplyMatrices(adaptation, fromToXYZ));
}

RGB ColorConverter::applyMatrix(const RGB& rgb, const Matrix3x3& matrix) {
    float r = matrix[0][0] * rgb.r + matrix[0][1] * rgb.g + matrix[0][2] * rgb.b;
    float g = matrix[1][0] * rgb.r + matrix[1][1] * rgb.g + matrix[1][2] * rgb.b;
    float b = matrix[2][0] * rgb.r + matrix[2][1] * rgb.g + matrix[2][2] * rgb.b;

    // 限制在合理范围内 (允许负值用于wide gamut转换)
    return RGB(r, g, b);
}

ColorConverter::Matrix3x3 ColorConverter::multiplyMatrices(const Matrix3x3& a, const Matrix3x3& b) {
    Matrix3x3 result;

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            result[i][j] = 0;
            for (int k = 0; k < 3; ++k) {
                result[i][j] += a[i][k] * b[k][j];
            }
        }
    }

    return result;
}

ColorConverter::Matrix3x3 ColorConverter::invertMatrix(const Matrix3x3& m) {
    Matrix3x3 inv;

    // 计算行列式
    float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
              - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
              + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);

    if (std::abs(det) < 1e-10) {
        throw std::runtime_error("矩阵不可逆");
    }

    float invDet = 1.0f / det;

    // 计算伴随矩阵并除以行列式
    inv[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * invDet;
    inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * invDet;
    inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * invDet;

    inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * invDet;
    inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * invDet;
    inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * invDet;

    inv[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * invDet;
    inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * invDet;
    inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * invDet;

    return inv;
}

// ============================================================================
// 色彩空间矩阵定义
// ============================================================================

ColorConverter::Matrix3x3 ColorConverter::getSonyNativeToXYZ() {
    // Sony 相机的原生色彩空间矩阵
    // 这是一个近似值,实际应该从相机的 color matrix 中获取
    // 这里使用类似 sRGB 的矩阵作为占位符
    // 在实际使用中,应该从 libraw 的 imgdata.color.cam_xyz 获取
    return Matrix3x3{{
        {0.4124564f, 0.3575761f, 0.1804375f},
        {0.2126729f, 0.7151522f, 0.0721750f},
        {0.0193339f, 0.1191920f, 0.9503041f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getFGamutToXYZ() {
    // Fuji F-Gamut 到 XYZ 的转换矩阵
    // F-Gamut primaries match Rec.2020 (D65)
    return Matrix3x3{{
        {0.6369580f, 0.1446169f, 0.1688810f},
        {0.2627002f, 0.6779981f, 0.0593017f},
        {0.0000000f, 0.0280727f, 1.0609851f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getACES2065ToXYZ() {
    return Matrix3x3{{
        {0.9525524f, 0.0000000f, 0.0000937f},
        {0.3439664f, 0.7281661f, -0.0721325f},
        {0.0000000f, 0.0000000f, 1.0088252f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getACEScgToXYZ() {
    return Matrix3x3{{
        {0.6624542f, 0.1340042f, 0.1561877f},
        {0.2722287f, 0.6740818f, 0.0536895f},
        {-0.0055746f, 0.0040607f, 1.0103391f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getBT709ToXYZ() {
    // ITU-R BT.709 (HD TV standard) 到 XYZ
    return Matrix3x3{{
        {0.4124564f, 0.3575761f, 0.1804375f},
        {0.2126729f, 0.7151522f, 0.0721750f},
        {0.0193339f, 0.1191920f, 0.9503041f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getProPhotoToXYZ() {
    // ProPhoto RGB 到 XYZ
    return Matrix3x3{{
        {0.7976749f, 0.1351917f, 0.0313534f},
        {0.2880402f, 0.7118741f, 0.0000857f},
        {0.0000000f, 0.0000000f, 0.8252100f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getAdobeRGBToXYZ() {
    return Matrix3x3{{
        {0.5767309f, 0.1855540f, 0.1881852f},
        {0.2973769f, 0.6273491f, 0.0752741f},
        {0.0270343f, 0.0706872f, 0.9911085f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getSRGBToXYZ() {
    // sRGB 到 XYZ (D65)
    return Matrix3x3{{
        {0.4124564f, 0.3575761f, 0.1804375f},
        {0.2126729f, 0.7151522f, 0.0721750f},
        {0.0193339f, 0.1191920f, 0.9503041f}
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getXYZToColorSpace(ColorSpace cs) {
    Matrix3x3 toXYZ;

    switch (cs) {
        case ColorSpace::SonyNative:
            toXYZ = getSonyNativeToXYZ();
            break;
        case ColorSpace::FujiFilm_FGamut:
            toXYZ = getFGamutToXYZ();
            break;
        case ColorSpace::ACES2065_1:
            toXYZ = getACES2065ToXYZ();
            break;
        case ColorSpace::ACEScg:
            toXYZ = getACEScgToXYZ();
            break;
        case ColorSpace::BT709:
            toXYZ = getBT709ToXYZ();
            break;
        case ColorSpace::ProPhotoRGB:
            toXYZ = getProPhotoToXYZ();
            break;
        case ColorSpace::AdobeRGB:
            toXYZ = getAdobeRGBToXYZ();
            break;
        case ColorSpace::sRGB:
            toXYZ = getSRGBToXYZ();
            break;
    }

    return invertMatrix(toXYZ);
}

ColorConverter::Matrix3x3 ColorConverter::identityMatrix() {
    return Matrix3x3{{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    }};
}

ColorConverter::XYZ ColorConverter::getWhitePointXYZ(ColorSpace cs) {
    static const XYZ whitepoints[] = {
        XYZ{{0.95047f, 1.00000f, 1.08883f}},  // SonyNative (D65)
        XYZ{{0.95047f, 1.00000f, 1.08883f}},  // FujiFilm_FGamut (D65)
        XYZ{{0.9520f, 1.0000f, 1.0080f}},     // ACEScg (D60)
        XYZ{{0.95047f, 1.00000f, 1.08883f}},  // BT709 (D65)
        XYZ{{0.96422f, 1.00000f, 0.82521f}},  // ProPhotoRGB (D50)
        XYZ{{0.95047f, 1.00000f, 1.08883f}},
        XYZ{{0.95047f, 1.00000f, 1.08883f}},  // sRGB (D65)
        XYZ{{0.9520f, 1.0000f, 1.0080f}}      // ACES2065-1 (D60)
    };

    size_t index = static_cast<size_t>(cs);
    if (index >= (sizeof(whitepoints) / sizeof(whitepoints[0]))) {
        return whitepoints[static_cast<size_t>(ColorSpace::sRGB)];
    }

    return whitepoints[index];
}

ColorConverter::XYZ ColorConverter::multiplyXYZ(const Matrix3x3& matrix, const XYZ& value) {
    return XYZ{{
        matrix[0][0] * value[0] + matrix[0][1] * value[1] + matrix[0][2] * value[2],
        matrix[1][0] * value[0] + matrix[1][1] * value[1] + matrix[1][2] * value[2],
        matrix[2][0] * value[0] + matrix[2][1] * value[1] + matrix[2][2] * value[2]
    }};
}

ColorConverter::Matrix3x3 ColorConverter::getChromaticAdaptationMatrix(
    ColorSpace fromWhitePoint,
    ColorSpace toWhitePoint
) {
    if (fromWhitePoint == toWhitePoint) {
        return identityMatrix();
    }

    const Matrix3x3 bradford = Matrix3x3{{
        {0.8951f, 0.2664f, -0.1614f},
        {-0.7502f, 1.7135f, 0.0367f},
        {0.0389f, -0.0685f, 1.0296f}
    }};
    Matrix3x3 bradfordInv = invertMatrix(bradford);

    XYZ src = getWhitePointXYZ(fromWhitePoint);
    XYZ dst = getWhitePointXYZ(toWhitePoint);
    XYZ srcCone = multiplyXYZ(bradford, src);
    XYZ dstCone = multiplyXYZ(bradford, dst);

    Matrix3x3 scale = Matrix3x3{{
        {dstCone[0] / srcCone[0], 0.0f, 0.0f},
        {0.0f, dstCone[1] / srcCone[1], 0.0f},
        {0.0f, 0.0f, dstCone[2] / srcCone[2]}
    }};

    return multiplyMatrices(bradfordInv, multiplyMatrices(scale, bradford));
}

// ============================================================================
// GammaConverter Implementation
// ============================================================================

namespace {

// ============================================================================
// F-Log2 helpers (soft-knee + percentile scaling).
// ============================================================================
constexpr float kLowPercentile = 0.005f;
constexpr float kHighPercentile = 0.995f;
constexpr size_t kMaxSamples = 200000;
constexpr float kSoftKneeScale = 0.5f;
constexpr float kSoftKneeMin = 1e-6f;
constexpr float kSoftplusClamp = 20.0f;

float clampPercentile(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float selectPercentile(std::vector<float>& values, float percentile) {
    if (values.empty()) {
        return 0.0f;
    }

    float pct = clampPercentile(percentile);
    size_t index = static_cast<size_t>(std::round((values.size() - 1) * pct));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

float softplus(float value, float knee) {
    if (knee <= 0.0f) {
        return std::max(0.0f, value);
    }

    float scaled = value / knee;
    if (scaled > kSoftplusClamp) {
        return value;
    }
    if (scaled < -kSoftplusClamp) {
        return 0.0f;
    }

    return knee * std::log1p(std::exp(scaled));
}

} // namespace

float GammaConverter::applyGamma(float linear, float gamma) {
    if (linear <= 0.0f) return 0.0f;
    return std::pow(linear, 1.0f / gamma);
}

float GammaConverter::removeGamma(float encoded, float gamma) {
    if (encoded <= 0.0f) return 0.0f;
    return std::pow(encoded, gamma);
}

float GammaConverter::applySRGBGamma(float linear) {
    if (linear <= 0.0031308f) {
        return 12.92f * linear;
    } else {
        return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }
}

float GammaConverter::removeSRGBGamma(float encoded) {
    if (encoded <= 0.04045f) {
        return encoded / 12.92f;
    } else {
        return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }
}

float GammaConverter::applyFLog2(float linear) {
    const float a = 5.555556f;
    const float b = 0.064829f;
    const float c = 0.245281f;
    const float d = 0.384316f;
    const float e = 8.799461f;
    const float f = 0.092864f;
    const float cut = 0.00088899597f;

    float lin = std::max(0.0f, linear);

    if (lin < cut) {
        return std::max(0.0f, std::min(1.0f, e * lin + f));
    }

    float encoded = std::log10(lin * a + b) * c + d;
    return std::max(0.0f, std::min(1.0f, encoded));
}

RGB GammaConverter::applyFLog2ToRGB(const RGB& rgb) {
    return RGB(
        applyFLog2(rgb.r),
        applyFLog2(rgb.g),
        applyFLog2(rgb.b)
    );
}

GammaConverter::FLog2Diagnostics GammaConverter::applyFLog2ToImage(
    ImageData& image,
    const FLog2Options& options
) {
    FLog2Diagnostics diagnostics;
    if (image.pixels.empty()) {
        return diagnostics;
    }

    size_t pixel_count = image.pixels.size();
    if (options.clampOnly) {
        float min_linear = std::numeric_limits<float>::max();
        float max_linear = std::numeric_limits<float>::lowest();
        size_t negative_pixels = 0;
        size_t over_pixels = 0;

#ifdef _OPENMP
#pragma omp parallel for reduction(min:min_linear) reduction(max:max_linear) \
    reduction(+:negative_pixels, over_pixels) if (pixel_count >= kParallelThreshold)
#endif
        for (size_t i = 0; i < pixel_count; ++i) {
            auto& pixel = image.pixels[i];
            float min_c = std::min(pixel.r, std::min(pixel.g, pixel.b));
            float max_c = std::max(pixel.r, std::max(pixel.g, pixel.b));

            min_linear = std::min(min_linear, min_c);
            max_linear = std::max(max_linear, max_c);

            if (min_c < 0.0f) {
                ++negative_pixels;
            }
            if (max_c > 1.0f) {
                ++over_pixels;
            }

            RGB clamped(
                clamp01(pixel.r),
                clamp01(pixel.g),
                clamp01(pixel.b)
            );
            pixel = applyFLog2ToRGB(clamped);
        }

        diagnostics.min_linear = min_linear;
        diagnostics.max_linear = max_linear;
        diagnostics.negative_pixels = negative_pixels;
        diagnostics.over_pixels = over_pixels;
        return diagnostics;
    }

    diagnostics.min_linear = std::numeric_limits<float>::max();
    diagnostics.max_linear = std::numeric_limits<float>::lowest();

    size_t sample_target = std::min(kMaxSamples, pixel_count);
    size_t sample_step = std::max<size_t>(1, pixel_count / std::max<size_t>(1, sample_target));

    std::vector<float> min_samples;
    std::vector<float> max_samples;
    min_samples.reserve(sample_target);
    max_samples.reserve(sample_target);

    for (size_t i = 0; i < pixel_count; ++i) {
        const auto& pixel = image.pixels[i];
        float min_c = std::min(pixel.r, std::min(pixel.g, pixel.b));
        float max_c = std::max(pixel.r, std::max(pixel.g, pixel.b));

        diagnostics.min_linear = std::min(diagnostics.min_linear, min_c);
        diagnostics.max_linear = std::max(diagnostics.max_linear, max_c);

        if (min_c < 0.0f) {
            diagnostics.negative_pixels++;
        }
        if (max_c > 1.0f) {
            diagnostics.over_pixels++;
        }

        if (i % sample_step == 0) {
            min_samples.push_back(min_c);
            max_samples.push_back(max_c);
        }
    }

    float low_p = selectPercentile(min_samples, kLowPercentile);
    float high_p = selectPercentile(max_samples, kHighPercentile);
    if (low_p > high_p) {
        std::swap(low_p, high_p);
    }

    float knee = 0.0f;
    if (low_p < 0.0f) {
        knee = std::max(kSoftKneeMin, -low_p * kSoftKneeScale);
    }
    diagnostics.offset = (knee > 0.0f) ? (knee * std::log(2.0f)) : 0.0f;

    float high_mapped = softplus(high_p, knee);
    if (options.normalizeToRange && high_mapped > 1.0f) {
        diagnostics.scale = 1.0f / high_mapped;
    }

#ifdef _OPENMP
#pragma omp parallel for if (pixel_count >= kParallelThreshold)
#endif
    for (size_t i = 0; i < pixel_count; ++i) {
        auto& pixel = image.pixels[i];
        RGB scaled(
            softplus(pixel.r, knee) * diagnostics.scale,
            softplus(pixel.g, knee) * diagnostics.scale,
            softplus(pixel.b, knee) * diagnostics.scale
        );
        pixel = applyFLog2ToRGB(scaled);
    }

    return diagnostics;
}

RGB GammaConverter::applyGammaToRGB(const RGB& rgb, float gamma) {
    return RGB(
        applyGamma(rgb.r, gamma),
        applyGamma(rgb.g, gamma),
        applyGamma(rgb.b, gamma)
    );
}

RGB GammaConverter::removeGammaFromRGB(const RGB& rgb, float gamma) {
    return RGB(
        removeGamma(rgb.r, gamma),
        removeGamma(rgb.g, gamma),
        removeGamma(rgb.b, gamma)
    );
}

} // namespace sony2fuji
