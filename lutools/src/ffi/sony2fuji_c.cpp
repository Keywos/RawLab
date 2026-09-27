#include "sony2fuji/ffi/sony2fuji_c.h"

#include "sony2fuji/sony2fuji.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

struct sony2fuji_session {
    sony2fuji::GpuConfig gpu_config;
};

namespace {

// ============================================================================
// Utilities
// ============================================================================

bool isEmptyString(const char* value) {
    return value == nullptr || value[0] == '\0';
}

enum class AcesConversionMode {
    Disabled,
    AssumeAP0,
    AssumeAP1
};

AcesConversionMode getAcesConversionMode() {
    const char* value = std::getenv("SONY2FUJI_ACES_MODE");
    if (!value || value[0] == '\0') {
        return AcesConversionMode::Disabled;
    }

    std::string mode(value);
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (mode == "off" || mode == "disabled" || mode == "adobe") {
        return AcesConversionMode::Disabled;
    }

    if (mode == "ap1" || mode == "acescg" || mode == "skip") {
        return AcesConversionMode::AssumeAP1;
    }

    return AcesConversionMode::AssumeAP0;
}

float clampFloat(float value, float low, float high) {
    return std::max(low, std::min(high, value));
}

float clamp01(float value) {
    return clampFloat(value, 0.0f, 1.0f);
}

float signedPow(float value, float power) {
    float sign = value >= 0.0f ? 1.0f : -1.0f;
    return sign * std::pow(std::abs(value), power);
}

float lerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

sony2fuji::RGB lerpRGB(const sony2fuji::RGB& a, const sony2fuji::RGB& b, float t) {
    return sony2fuji::RGB(
        lerpFloat(a.r, b.r, t),
        lerpFloat(a.g, b.g, t),
        lerpFloat(a.b, b.b, t)
    );
}

float luminance(const sony2fuji::RGB& pixel) {
    return 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
}

struct ToneCurvePoints {
    float p1;
    float p2;
    float p3;
};

ToneCurvePoints buildToneCurve(float tone_curve) {
    float curve_strength = signedPow(tone_curve, 1.2f);
    float lift = curve_strength * 0.2f;
    float mid = curve_strength * 0.05f;

    ToneCurvePoints points;
    points.p1 = clamp01(0.25f - lift);
    points.p2 = clamp01(0.5f + mid);
    points.p3 = clamp01(0.75f + lift);
    return points;
}

float applyToneCurve(float x, const ToneCurvePoints& curve) {
    float value = clamp01(x);
    if (value <= 0.25f) {
        return lerpFloat(0.0f, curve.p1, value / 0.25f);
    }
    if (value <= 0.5f) {
        return lerpFloat(curve.p1, curve.p2, (value - 0.25f) / 0.25f);
    }
    if (value <= 0.75f) {
        return lerpFloat(curve.p2, curve.p3, (value - 0.5f) / 0.25f);
    }
    return lerpFloat(curve.p3, 1.0f, (value - 0.75f) / 0.25f);
}

float adjustShadows(float value, float shadows) {
    if (shadows == 0.0f) {
        return value;
    }
    float t = clampFloat(shadows, -1.0f, 1.0f);
    float gamma = t > 0.0f ? (1.0f - t * 0.5f) : (1.0f + (-t) * 0.5f);
    return std::pow(clamp01(value), gamma);
}

float adjustHighlights(float value, float highlights) {
    if (highlights == 0.0f) {
        return value;
    }
    float t = clampFloat(highlights, -1.0f, 1.0f);
    float inv = 1.0f - clamp01(value);
    float gamma = t > 0.0f ? (1.0f - t * 0.5f) : (1.0f + (-t) * 0.5f);
    return 1.0f - std::pow(inv, gamma);
}

float applyHighlightsShadows(float value, float highlights, float shadows) {
    float shadowed = adjustShadows(value, shadows);
    return adjustHighlights(shadowed, highlights);
}

sony2fuji::RGB normalizeWhiteBalance(float r, float g, float b) {
    float max_value = std::max(r, std::max(g, b));
    if (max_value <= 0.0f) {
        return sony2fuji::RGB(1.0f, 1.0f, 1.0f);
    }
    return sony2fuji::RGB(r / max_value, g / max_value, b / max_value);
}

sony2fuji::RGB temperatureToRGB(float temperature) {
    float temp = clampFloat(temperature, 1000.0f, 40000.0f) / 100.0f;
    float r;
    float g;
    float b;

    if (temp <= 66.0f) {
        r = 1.0f;
        g = clamp01(0.3900816f * std::log(temp) - 0.6318414f);
        if (temp <= 19.0f) {
            b = 0.0f;
        } else {
            b = clamp01(0.5432068f * std::log(temp - 10.0f) - 1.1962541f);
        }
    } else {
        r = clamp01(1.2929362f * std::pow(temp - 60.0f, -0.1332048f));
        g = clamp01(1.1298909f * std::pow(temp - 60.0f, -0.0755148f));
        b = 1.0f;
    }

    return normalizeWhiteBalance(r, g, b);
}

sony2fuji::RGB applyTint(const sony2fuji::RGB& base, float tint) {
    float t = clampFloat(tint / 100.0f, -1.0f, 1.0f);
    float g = base.g * (1.0f + t * 0.1f);
    return normalizeWhiteBalance(base.r, g, base.b);
}

sony2fuji::RGB whiteBalanceForTempTint(float temperature, float tint) {
    if (temperature <= 0.0f) {
        return sony2fuji::RGB(1.0f, 1.0f, 1.0f);
    }
    return applyTint(temperatureToRGB(temperature), tint);
}

void applyExposureAndWhiteBalance(
    sony2fuji::RGB& pixel,
    float exposure_scale,
    const sony2fuji::RGB& wb
) {
    pixel.r *= exposure_scale * wb.r;
    pixel.g *= exposure_scale * wb.g;
    pixel.b *= exposure_scale * wb.b;
}

void applyHighlightsShadowsToPixel(
    sony2fuji::RGB& pixel,
    float highlights,
    float shadows
) {
    float l = luminance(pixel);
    float adjusted = applyHighlightsShadows(l, highlights, shadows);
    if (l > 0.0f) {
        float ratio = adjusted / l;
        pixel.r *= ratio;
        pixel.g *= ratio;
        pixel.b *= ratio;
        return;
    }
    pixel.r = adjusted;
    pixel.g = adjusted;
    pixel.b = adjusted;
}

void applyContrastSaturation(
    sony2fuji::RGB& pixel,
    float contrast,
    float saturation
) {
    pixel.r = (pixel.r - 0.5f) * contrast + 0.5f;
    pixel.g = (pixel.g - 0.5f) * contrast + 0.5f;
    pixel.b = (pixel.b - 0.5f) * contrast + 0.5f;

    float l = luminance(pixel);
    pixel.r = l + (pixel.r - l) * saturation;
    pixel.g = l + (pixel.g - l) * saturation;
    pixel.b = l + (pixel.b - l) * saturation;
}

bool needsToneAdjustments(const sony2fuji_request& request) {
    if (request.exposure_ev != 0.0f || request.brightness != 1.0f) {
        return true;
    }
    if (request.contrast != 1.0f || request.saturation != 1.0f) {
        return true;
    }
    if (request.temperature != 6500.0f || request.tint != 0.0f) {
        return true;
    }
    if (request.wb_mode == SONY2FUJI_WB_CUSTOM) {
        return true;
    }
    if (request.highlights != 0.0f || request.shadows != 0.0f) {
        return true;
    }
    return request.tone_curve != 0.0f;
}
sony2fuji_status mapError(sony2fuji::ErrorCode code) {
    switch (code) {
        case sony2fuji::ErrorCode::Success:
            return SONY2FUJI_STATUS_OK;
        case sony2fuji::ErrorCode::FileNotFound:
            return SONY2FUJI_STATUS_IO_ERROR;
        case sony2fuji::ErrorCode::ParseError:
        case sony2fuji::ErrorCode::InvalidFormat:
            return SONY2FUJI_STATUS_UNSUPPORTED;
        case sony2fuji::ErrorCode::OutOfMemory:
            return SONY2FUJI_STATUS_OUT_OF_MEMORY;
        case sony2fuji::ErrorCode::ProcessingError:
        default:
            return SONY2FUJI_STATUS_PROCESSING_ERROR;
    }
}

sony2fuji::ColorSpace toCoreColorSpace(sony2fuji_color_space space) {
    switch (space) {
        case SONY2FUJI_COLOR_FGAMUT:
            return sony2fuji::ColorSpace::FujiFilm_FGamut;
        case SONY2FUJI_COLOR_SONY_NATIVE:
            return sony2fuji::ColorSpace::SonyNative;
        case SONY2FUJI_COLOR_ACESCG:
            return sony2fuji::ColorSpace::ACEScg;
        case SONY2FUJI_COLOR_ADOBE_RGB:
            return sony2fuji::ColorSpace::AdobeRGB;
        case SONY2FUJI_COLOR_SRGB:
        default:
            return sony2fuji::ColorSpace::sRGB;
    }
}

sony2fuji::GpuMode toCoreGpuMode(sony2fuji_gpu_mode mode) {
    switch (mode) {
        case SONY2FUJI_GPU_AUTO:
            return sony2fuji::GpuMode::Auto;
        case SONY2FUJI_GPU_FORCE:
            return sony2fuji::GpuMode::Force;
        case SONY2FUJI_GPU_OFF:
        default:
            return sony2fuji::GpuMode::Off;
    }
}

sony2fuji_status validateRequest(
    const sony2fuji_request* request,
    const sony2fuji_buffer* out_buffer
) {
    if (!request) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    if (request->version != SONY2FUJI_REQUEST_VERSION) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    if (request->struct_size < sizeof(sony2fuji_request)) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    if (request->input_type == SONY2FUJI_INPUT_RAW) {
        if (isEmptyString(request->input_path)) {
            return SONY2FUJI_STATUS_INVALID_ARGUMENT;
        }
    } else if (request->input_type == SONY2FUJI_INPUT_BUFFER) {
        if (!request->input_pixels || request->input_width == 0 || request->input_height == 0) {
            return SONY2FUJI_STATUS_INVALID_ARGUMENT;
        }
    } else {
        return SONY2FUJI_STATUS_UNSUPPORTED;
    }

    if (request->output_target == SONY2FUJI_TARGET_FILE) {
        if (isEmptyString(request->output_path)) {
            return SONY2FUJI_STATUS_INVALID_ARGUMENT;
        }
    } else if (request->output_target == SONY2FUJI_TARGET_BUFFER) {
        if (!out_buffer) {
            return SONY2FUJI_STATUS_INVALID_ARGUMENT;
        }
    } else {
        return SONY2FUJI_STATUS_UNSUPPORTED;
    }

    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status validateGpuConfig(const sony2fuji_gpu_config* config) {
    if (!config) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }
    if (config->version != SONY2FUJI_GPU_CONFIG_VERSION) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }
    if (config->struct_size < sizeof(sony2fuji_gpu_config)) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }
    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status validateOutputFormat(const sony2fuji_request& request) {
    if (request.output_target == SONY2FUJI_TARGET_FILE) {
        if (request.output_format == SONY2FUJI_OUTPUT_JPEG ||
            request.output_format == SONY2FUJI_OUTPUT_PNG) {
            return SONY2FUJI_STATUS_OK;
        }
        return SONY2FUJI_STATUS_UNSUPPORTED;
    }

    if (request.output_format == SONY2FUJI_OUTPUT_RGB8 ||
        request.output_format == SONY2FUJI_OUTPUT_RGBA8) {
        return SONY2FUJI_STATUS_OK;
    }

    return SONY2FUJI_STATUS_UNSUPPORTED;
}

sony2fuji_status computeTargetSize(
    const sony2fuji_request& request,
    uint32_t src_width,
    uint32_t src_height,
    uint32_t* out_width,
    uint32_t* out_height
) {
    if (!out_width || !out_height || src_width == 0 || src_height == 0) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    sony2fuji_size_mode mode = request.size_mode;
    uint32_t long_edge = request.long_edge;
    uint32_t short_edge = request.short_edge;

    if (request.intent == SONY2FUJI_INTENT_PREVIEW && request.preview_long_edge > 0) {
        mode = SONY2FUJI_SIZE_FIT_LONG_EDGE;
        long_edge = request.preview_long_edge;
    }

    switch (mode) {
        case SONY2FUJI_SIZE_EXACT:
            if (request.target_width == 0 || request.target_height == 0) {
                return SONY2FUJI_STATUS_INVALID_ARGUMENT;
            }
            *out_width = request.target_width;
            *out_height = request.target_height;
            return SONY2FUJI_STATUS_OK;
        case SONY2FUJI_SIZE_FIT_LONG_EDGE: {
            if (long_edge == 0) {
                return SONY2FUJI_STATUS_INVALID_ARGUMENT;
            }
            uint32_t max_edge = std::max(src_width, src_height);
            float scale = static_cast<float>(long_edge) / static_cast<float>(max_edge);
            *out_width = std::max(1u, static_cast<uint32_t>(std::round(src_width * scale)));
            *out_height = std::max(1u, static_cast<uint32_t>(std::round(src_height * scale)));
            return SONY2FUJI_STATUS_OK;
        }
        case SONY2FUJI_SIZE_FIT_SHORT_EDGE: {
            if (short_edge == 0) {
                return SONY2FUJI_STATUS_INVALID_ARGUMENT;
            }
            uint32_t min_edge = std::min(src_width, src_height);
            float scale = static_cast<float>(short_edge) / static_cast<float>(min_edge);
            *out_width = std::max(1u, static_cast<uint32_t>(std::round(src_width * scale)));
            *out_height = std::max(1u, static_cast<uint32_t>(std::round(src_height * scale)));
            return SONY2FUJI_STATUS_OK;
        }
        default:
            return SONY2FUJI_STATUS_UNSUPPORTED;
    }
}

sony2fuji::RGB sampleBilinear(const sony2fuji::ImageData& image, float x, float y) {
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    int x1 = std::min(x0 + 1, image.width - 1);
    int y1 = std::min(y0 + 1, image.height - 1);

    float tx = x - static_cast<float>(x0);
    float ty = y - static_cast<float>(y0);

    const sony2fuji::RGB& c00 = image.at(x0, y0);
    const sony2fuji::RGB& c10 = image.at(x1, y0);
    const sony2fuji::RGB& c01 = image.at(x0, y1);
    const sony2fuji::RGB& c11 = image.at(x1, y1);

    sony2fuji::RGB cx0 = lerpRGB(c00, c10, tx);
    sony2fuji::RGB cx1 = lerpRGB(c01, c11, tx);

    return lerpRGB(cx0, cx1, ty);
}

sony2fuji::ImageData resizeBilinear(
    const sony2fuji::ImageData& source,
    uint32_t target_width,
    uint32_t target_height
) {
    sony2fuji::ImageData output(static_cast<int>(target_width), static_cast<int>(target_height));

    if (source.width == 0 || source.height == 0) {
        return output;
    }

    float scale_x = (target_width == 1)
        ? 0.0f
        : static_cast<float>(source.width - 1) / static_cast<float>(target_width - 1);
    float scale_y = (target_height == 1)
        ? 0.0f
        : static_cast<float>(source.height - 1) / static_cast<float>(target_height - 1);

    for (uint32_t y = 0; y < target_height; ++y) {
        float src_y = static_cast<float>(y) * scale_y;
        for (uint32_t x = 0; x < target_width; ++x) {
            float src_x = static_cast<float>(x) * scale_x;
            output.pixels[static_cast<size_t>(y) * target_width + x] = sampleBilinear(source, src_x, src_y);
        }
    }

    return output;
}

void applyToneAdjustments(sony2fuji::ImageData& image, const sony2fuji_request& request) {
    if (!needsToneAdjustments(request)) {
        return;
    }

    float exposure_scale = std::pow(2.0f, request.exposure_ev) * request.brightness;
    float contrast = clampFloat(request.contrast, 0.0f, 2.0f);
    float saturation = clampFloat(request.saturation, 0.0f, 2.0f);
    float highlights = clampFloat(request.highlights, -1.0f, 1.0f);
    float shadows = clampFloat(request.shadows, -1.0f, 1.0f);
    bool use_curve = request.tone_curve != 0.0f;
    ToneCurvePoints curve = buildToneCurve(request.tone_curve);
    sony2fuji::RGB wb = whiteBalanceForTempTint(request.temperature, request.tint);
    if (request.wb_mode == SONY2FUJI_WB_CUSTOM) {
        wb = normalizeWhiteBalance(
            wb.r * request.wb_mul[0],
            wb.g * request.wb_mul[1],
            wb.b * request.wb_mul[2]
        );
    }

    for (auto& pixel : image.pixels) {
        applyExposureAndWhiteBalance(pixel, exposure_scale, wb);
        if (highlights != 0.0f || shadows != 0.0f) {
            applyHighlightsShadowsToPixel(pixel, highlights, shadows);
        }
        if (use_curve) {
            pixel.r = applyToneCurve(pixel.r, curve);
            pixel.g = applyToneCurve(pixel.g, curve);
            pixel.b = applyToneCurve(pixel.b, curve);
        }
        if (contrast != 1.0f || saturation != 1.0f) {
            applyContrastSaturation(pixel, contrast, saturation);
        }
    }
}

sony2fuji::RGB sumRow3(const sony2fuji::ImageData& image, int y, int x0, int x1, int x2) {
    const sony2fuji::RGB& c0 = image.at(x0, y);
    const sony2fuji::RGB& c1 = image.at(x1, y);
    const sony2fuji::RGB& c2 = image.at(x2, y);
    return sony2fuji::RGB(c0.r + c1.r + c2.r, c0.g + c1.g + c2.g, c0.b + c1.b + c2.b);
}

std::vector<sony2fuji::RGB> blurImage(const sony2fuji::ImageData& image) {
    std::vector<sony2fuji::RGB> output(image.pixels.size());
    int width = image.width;
    int height = image.height;
    if (width <= 0 || height <= 0) {
        return output;
    }

    for (int y = 0; y < height; ++y) {
        int y0 = std::max(0, y - 1);
        int y1 = y;
        int y2 = std::min(height - 1, y + 1);
        for (int x = 0; x < width; ++x) {
            int x0 = std::max(0, x - 1);
            int x1 = x;
            int x2 = std::min(width - 1, x + 1);
            sony2fuji::RGB sum = sumRow3(image, y0, x0, x1, x2);
            sony2fuji::RGB row1 = sumRow3(image, y1, x0, x1, x2);
            sony2fuji::RGB row2 = sumRow3(image, y2, x0, x1, x2);
            sum.r += row1.r + row2.r;
            sum.g += row1.g + row2.g;
            sum.b += row1.b + row2.b;
            output[static_cast<size_t>(y) * width + x] = sony2fuji::RGB(
                sum.r / 9.0f,
                sum.g / 9.0f,
                sum.b / 9.0f
            );
        }
    }

    return output;
}

void applyNoiseReduction(sony2fuji::ImageData& image, float amount) {
    float strength = clampFloat(amount, 0.0f, 1.0f);
    if (strength <= 0.0f) {
        return;
    }

    std::vector<sony2fuji::RGB> blurred = blurImage(image);
    float blend = strength * 0.4f;
    for (size_t i = 0; i < image.pixels.size(); ++i) {
        image.pixels[i] = lerpRGB(image.pixels[i], blurred[i], blend);
    }
}

void applySharpening(sony2fuji::ImageData& image, float amount) {
    float strength = clampFloat(amount, 0.0f, 2.0f);
    if (strength <= 0.0f) {
        return;
    }

    std::vector<sony2fuji::RGB> blurred = blurImage(image);
    for (size_t i = 0; i < image.pixels.size(); ++i) {
        sony2fuji::RGB current = image.pixels[i];
        sony2fuji::RGB blur = blurred[i];
        current.r = clamp01(current.r + strength * (current.r - blur.r));
        current.g = clamp01(current.g + strength * (current.g - blur.g));
        current.b = clamp01(current.b + strength * (current.b - blur.b));
        image.pixels[i] = current;
    }
}

void applyDetailAdjustments(sony2fuji::ImageData& image, const sony2fuji_request& request) {
    if (request.noise_reduction > 0.0f) {
        applyNoiseReduction(image, request.noise_reduction);
    }
    if (request.sharpening > 0.0f) {
        applySharpening(image, request.sharpening);
    }
}

bool isLutDiagnosticsEnabled() {
    const char* value = std::getenv("SONY2FUJI_LUT_DIAG");
    return value != nullptr && value[0] != '\0';
}

void applyFLog2Encoding(sony2fuji::ImageData& image, bool clamp_only) {
    sony2fuji::GammaConverter::FLog2Options options;
    options.normalizeToRange = !clamp_only;
    options.clampOnly = clamp_only;
    options.enableDiagnostics = isLutDiagnosticsEnabled();

    sony2fuji::GammaConverter::FLog2Diagnostics diagnostics =
        sony2fuji::GammaConverter::applyFLog2ToImage(image, options);
    if (options.enableDiagnostics) {
        std::fprintf(
            stderr,
            "F-Log2 diagnostics: min=%f max=%f scale=%f offset=%f neg=%zu over=%zu\n",
            diagnostics.min_linear,
            diagnostics.max_linear,
            diagnostics.scale,
            diagnostics.offset,
            diagnostics.negative_pixels,
            diagnostics.over_pixels
        );
    }
}

sony2fuji_status ensureLinear(
    sony2fuji::ImageData& image,
    sony2fuji_color_space space,
    bool* is_linear
) {
    if (!is_linear || *is_linear) {
        return SONY2FUJI_STATUS_OK;
    }

    if (space != SONY2FUJI_COLOR_SRGB) {
        return SONY2FUJI_STATUS_UNSUPPORTED;
    }

    for (auto& pixel : image.pixels) {
        pixel.r = sony2fuji::GammaConverter::removeSRGBGamma(pixel.r);
        pixel.g = sony2fuji::GammaConverter::removeSRGBGamma(pixel.g);
        pixel.b = sony2fuji::GammaConverter::removeSRGBGamma(pixel.b);
    }

    *is_linear = true;
    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status ensureDisplaySRGB(
    sony2fuji::ImageData& image,
    sony2fuji_color_space* space,
    bool* is_linear
) {
    if (!space || !is_linear) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    if (*space != SONY2FUJI_COLOR_SRGB) {
        if (!(*is_linear)) {
            return SONY2FUJI_STATUS_UNSUPPORTED;
        }

        sony2fuji::ColorConverter converter;
        auto from = toCoreColorSpace(*space);
        auto to = sony2fuji::ColorSpace::sRGB;
        sony2fuji::ErrorCode result = converter.convertImage(image, from, to);
        if (result != sony2fuji::ErrorCode::Success) {
            return mapError(result);
        }

        *space = SONY2FUJI_COLOR_SRGB;
    }

    if (*is_linear) {
        for (auto& pixel : image.pixels) {
            pixel.r = sony2fuji::GammaConverter::applySRGBGamma(pixel.r);
            pixel.g = sony2fuji::GammaConverter::applySRGBGamma(pixel.g);
            pixel.b = sony2fuji::GammaConverter::applySRGBGamma(pixel.b);
        }
        *is_linear = false;
    }

    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status convertToFGamut(
    sony2fuji::ImageData& image,
    sony2fuji_color_space* space,
    bool* is_linear,
    const sony2fuji::ColorConverter::Matrix3x3* camera_to_xyz,
    bool has_camera_matrix
) {
    if (!space || !is_linear) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    sony2fuji_status status = ensureLinear(image, *space, is_linear);
    if (status != SONY2FUJI_STATUS_OK) {
        return status;
    }

    if (*space == SONY2FUJI_COLOR_FGAMUT) {
        return SONY2FUJI_STATUS_OK;
    }

    sony2fuji::ColorConverter converter;

    if (*space == SONY2FUJI_COLOR_ADOBE_RGB) {
        sony2fuji::ErrorCode result = converter.convertImage(
            image,
            sony2fuji::ColorSpace::AdobeRGB,
            sony2fuji::ColorSpace::FujiFilm_FGamut
        );
        if (result != sony2fuji::ErrorCode::Success) {
            return mapError(result);
        }

        *space = SONY2FUJI_COLOR_FGAMUT;
        *is_linear = true;
        return SONY2FUJI_STATUS_OK;
    }

    if (*space != SONY2FUJI_COLOR_ACESCG) {
        sony2fuji::ErrorCode result = sony2fuji::ErrorCode::Success;
        if (*space == SONY2FUJI_COLOR_SONY_NATIVE && has_camera_matrix && camera_to_xyz) {
            result = converter.convertImage(
                image,
                *camera_to_xyz,
                sony2fuji::ColorSpace::SonyNative,
                sony2fuji::ColorSpace::ACEScg
            );
        } else {
            auto from = toCoreColorSpace(*space);
            auto to = sony2fuji::ColorSpace::ACEScg;
            result = converter.convertImage(image, from, to);
        }
        if (result != sony2fuji::ErrorCode::Success) {
            return mapError(result);
        }
        *space = SONY2FUJI_COLOR_ACESCG;
    }

    if (*space != SONY2FUJI_COLOR_FGAMUT) {
        auto to = sony2fuji::ColorSpace::FujiFilm_FGamut;
        sony2fuji::ErrorCode result = converter.convertImage(image, sony2fuji::ColorSpace::ACEScg, to);
        if (result != sony2fuji::ErrorCode::Success) {
            return mapError(result);
        }
    }

    *space = SONY2FUJI_COLOR_FGAMUT;
    *is_linear = true;
    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status loadLUT(
    const char* lut_path,
    std::shared_ptr<sony2fuji::LUT3D>* out_lut
) {
    if (!out_lut || isEmptyString(lut_path)) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    auto lut = sony2fuji::LUTParser::loadLUTCached(lut_path);
    if (!lut) {
        return SONY2FUJI_STATUS_UNSUPPORTED;
    }

    *out_lut = lut;
    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status applyLUTWithStrength(
    const sony2fuji_request& request,
    sony2fuji::ImageData& image,
    const sony2fuji::GpuConfig& gpu_config
) {
    float strength = clampFloat(request.lut_strength, 0.0f, 2.0f);
    if (strength <= 0.0f) {
        return SONY2FUJI_STATUS_OK;
    }

    std::shared_ptr<sony2fuji::LUT3D> lut;
    sony2fuji_status status = loadLUT(request.lut_path, &lut);
    if (status != SONY2FUJI_STATUS_OK) {
        return status;
    }

    if (strength == 1.0f) {
        return mapError(sony2fuji::applyLUTWithConfig(lut, image, gpu_config));
    }

    sony2fuji::ImageData base = image;
    sony2fuji::ErrorCode result = sony2fuji::applyLUTWithConfig(lut, image, gpu_config);
    if (result != sony2fuji::ErrorCode::Success) {
        return mapError(result);
    }

    for (size_t i = 0; i < image.pixels.size(); ++i) {
        sony2fuji::RGB mixed = lerpRGB(base.pixels[i], image.pixels[i], strength);
        image.pixels[i].r = clampFloat(mixed.r, 0.0f, 1.0f);
        image.pixels[i].g = clampFloat(mixed.g, 0.0f, 1.0f);
        image.pixels[i].b = clampFloat(mixed.b, 0.0f, 1.0f);
    }

    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status loadRawImage(
    const sony2fuji_request& request,
    sony2fuji::ImageData& image,
    sony2fuji_color_space* space,
    bool* is_linear,
    sony2fuji::ColorConverter::Matrix3x3* camera_to_xyz,
    bool* has_camera_matrix
) {
    sony2fuji::RAWProcessor processor;
    sony2fuji::ErrorCode result = processor.loadFile(request.input_path);
    if (result != sony2fuji::ErrorCode::Success) {
        return mapError(result);
    }

    sony2fuji::RAWProcessOptions options;
    options.useAutoWhiteBalance = request.wb_mode == SONY2FUJI_WB_AUTO;
    options.useCameraWhiteBalance = request.wb_mode == SONY2FUJI_WB_CAMERA;
    options.useCustomWhiteBalance = request.wb_mode == SONY2FUJI_WB_CUSTOM;
    options.customWhiteBalance[0] = request.wb_mul[0];
    options.customWhiteBalance[1] = request.wb_mul[1];
    options.customWhiteBalance[2] = request.wb_mul[2];
    options.customWhiteBalance[3] = request.wb_mul[3];
    options.exposure = 0.0f;
    options.brightness = 1.0f;
    options.outputLinear = !isEmptyString(request.lut_path) && request.lut_strength > 0.0f;
    AcesConversionMode aces_mode = getAcesConversionMode();
    options.outputAces = options.outputLinear && aces_mode != AcesConversionMode::Disabled;
    options.outputAdobe = options.outputLinear && aces_mode == AcesConversionMode::Disabled;

    result = processor.process(options, image);
    if (result != sony2fuji::ErrorCode::Success) {
        return mapError(result);
    }

    if (!space || !is_linear) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    if (camera_to_xyz && has_camera_matrix) {
        float camera_matrix[3][3];
        *has_camera_matrix = processor.getCameraColorMatrix(camera_matrix);
        if (*has_camera_matrix) {
            *camera_to_xyz = sony2fuji::ColorConverter::Matrix3x3{{
                {camera_matrix[0][0], camera_matrix[0][1], camera_matrix[0][2]},
                {camera_matrix[1][0], camera_matrix[1][1], camera_matrix[1][2]},
                {camera_matrix[2][0], camera_matrix[2][1], camera_matrix[2][2]}
            }};
        }
    }

    if (options.outputLinear) {
        if (options.outputAces) {
            if (aces_mode == AcesConversionMode::AssumeAP0) {
                sony2fuji::ColorConverter converter;
                result = converter.convertImage(
                    image,
                    sony2fuji::ColorSpace::ACES2065_1,
                    sony2fuji::ColorSpace::ACEScg
                );
                if (result != sony2fuji::ErrorCode::Success) {
                    return mapError(result);
                }
            }

            *space = SONY2FUJI_COLOR_ACESCG;
            *is_linear = true;
            return SONY2FUJI_STATUS_OK;
        }

        if (options.outputAdobe) {
            *space = SONY2FUJI_COLOR_ADOBE_RGB;
        } else {
            *space = SONY2FUJI_COLOR_SONY_NATIVE;
        }
        *is_linear = true;
        return SONY2FUJI_STATUS_OK;
    }

    *space = SONY2FUJI_COLOR_SRGB;
    *is_linear = false;
    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status loadBufferImage(
    const sony2fuji_request& request,
    sony2fuji::ImageData& image,
    sony2fuji_color_space* space,
    bool* is_linear
) {
    if (!request.input_pixels || request.input_width == 0 || request.input_height == 0) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    if (request.input_pixel_format != SONY2FUJI_PIXEL_RGB8 &&
        request.input_pixel_format != SONY2FUJI_PIXEL_RGBA8) {
        return SONY2FUJI_STATUS_UNSUPPORTED;
    }

    image.width = static_cast<int>(request.input_width);
    image.height = static_cast<int>(request.input_height);
    image.pixels.resize(static_cast<size_t>(request.input_width) * request.input_height);

    const uint8_t* data = static_cast<const uint8_t*>(request.input_pixels);
    uint32_t channels = request.input_pixel_format == SONY2FUJI_PIXEL_RGBA8 ? 4 : 3;

    for (size_t i = 0; i < image.pixels.size(); ++i) {
        size_t offset = i * channels;
        image.pixels[i].r = data[offset] / 255.0f;
        image.pixels[i].g = data[offset + 1] / 255.0f;
        image.pixels[i].b = data[offset + 2] / 255.0f;
    }

    if (space) {
        *space = request.input_color_space;
    }
    if (is_linear) {
        *is_linear = request.input_is_linear != 0;
    }

    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status writeOutputFile(
    const sony2fuji_request& request,
    const sony2fuji::ImageData& image
) {
    if (isEmptyString(request.output_path)) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    sony2fuji::OutputFormat format = sony2fuji::OutputFormat::JPEG;
    if (request.output_format == SONY2FUJI_OUTPUT_PNG) {
        format = sony2fuji::OutputFormat::PNG;
    }

    int quality = static_cast<int>(request.jpeg_quality);
    if (quality <= 0 || quality > 100) {
        quality = 95;
    }

    sony2fuji::ErrorCode result = sony2fuji::ImageEncoder::saveImage(
        image,
        request.output_path,
        format,
        quality
    );

    return mapError(result);
}

sony2fuji_status writeOutputBuffer(
    const sony2fuji_request& request,
    const sony2fuji::ImageData& image,
    sony2fuji_buffer* out_buffer
) {
    if (!out_buffer) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    uint32_t channels = request.output_format == SONY2FUJI_OUTPUT_RGBA8 ? 4 : 3;
    size_t stride = static_cast<size_t>(image.width) * channels;
    size_t total = stride * static_cast<size_t>(image.height);

    void* buffer = std::malloc(total);
    if (!buffer) {
        return SONY2FUJI_STATUS_OUT_OF_MEMORY;
    }

    uint8_t* dst = static_cast<uint8_t*>(buffer);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const sony2fuji::RGB& pixel = image.at(x, y);
            size_t offset = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * channels;
            dst[offset] = static_cast<uint8_t>(clampFloat(pixel.r, 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[offset + 1] = static_cast<uint8_t>(clampFloat(pixel.g, 0.0f, 1.0f) * 255.0f + 0.5f);
            dst[offset + 2] = static_cast<uint8_t>(clampFloat(pixel.b, 0.0f, 1.0f) * 255.0f + 0.5f);
            if (channels == 4) {
                dst[offset + 3] = 255;
            }
        }
    }

    out_buffer->data = buffer;
    out_buffer->size_bytes = total;
    out_buffer->width = static_cast<uint32_t>(image.width);
    out_buffer->height = static_cast<uint32_t>(image.height);
    out_buffer->stride_bytes = static_cast<uint32_t>(stride);
    out_buffer->pixel_format = (channels == 4) ? SONY2FUJI_PIXEL_RGBA8 : SONY2FUJI_PIXEL_RGB8;

    return SONY2FUJI_STATUS_OK;
}

} // namespace

// ============================================================================
// C API
// ============================================================================

sony2fuji_status sony2fuji_session_create(sony2fuji_session** out_session) {
    if (!out_session) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    *out_session = new (std::nothrow) sony2fuji_session();
    if (!*out_session) {
        return SONY2FUJI_STATUS_OUT_OF_MEMORY;
    }
    (*out_session)->gpu_config = sony2fuji::defaultGpuConfig();

    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status sony2fuji_session_destroy(sony2fuji_session* session) {
    delete session;
    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status sony2fuji_session_set_gpu_config(
    sony2fuji_session* session,
    const sony2fuji_gpu_config* config
) {
    if (!session) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }
    sony2fuji_status status = validateGpuConfig(config);
    if (status != SONY2FUJI_STATUS_OK) {
        return status;
    }
    session->gpu_config.mode = toCoreGpuMode(config->mode);
    return SONY2FUJI_STATUS_OK;
}

sony2fuji_status sony2fuji_process(
    sony2fuji_session* session,
    const sony2fuji_request* request,
    sony2fuji_buffer* out_buffer
) {
    if (!session) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }

    sony2fuji_status status = validateRequest(request, out_buffer);
    if (status != SONY2FUJI_STATUS_OK) {
        return status;
    }

    status = validateOutputFormat(*request);
    if (status != SONY2FUJI_STATUS_OK) {
        return status;
    }

    sony2fuji_request local = *request;
    local.lut_strength = clampFloat(local.lut_strength, 0.0f, 2.0f);

    sony2fuji::ImageData image;
    sony2fuji_color_space color_space = SONY2FUJI_COLOR_SRGB;
    bool is_linear = false;
    sony2fuji::ColorConverter::Matrix3x3 camera_to_xyz;
    bool has_camera_matrix = false;

    if (local.input_type == SONY2FUJI_INPUT_RAW) {
        status = loadRawImage(local, image, &color_space, &is_linear, &camera_to_xyz, &has_camera_matrix);
    } else {
        status = loadBufferImage(local, image, &color_space, &is_linear);
    }

    if (status != SONY2FUJI_STATUS_OK) {
        return status;
    }

    bool apply_before_lut = local.input_type == SONY2FUJI_INPUT_BUFFER;
    if (apply_before_lut) {
        applyToneAdjustments(image, local);
    }

    bool use_lut = !isEmptyString(local.lut_path) && local.lut_strength > 0.0f;
    bool clamp_flog2 = false;
    if (use_lut && local.input_type == SONY2FUJI_INPUT_RAW) {
        clamp_flog2 = getAcesConversionMode() == AcesConversionMode::Disabled;
    }

    if (use_lut) {
        status = convertToFGamut(image, &color_space, &is_linear, &camera_to_xyz, has_camera_matrix);
        if (status != SONY2FUJI_STATUS_OK) {
            return status;
        }

        applyFLog2Encoding(image, clamp_flog2);
        color_space = SONY2FUJI_COLOR_FGAMUT;
        is_linear = false;

        status = applyLUTWithStrength(local, image, session->gpu_config);
        if (status != SONY2FUJI_STATUS_OK) {
            return status;
        }
    } else {
        status = ensureDisplaySRGB(image, &color_space, &is_linear);
        if (status != SONY2FUJI_STATUS_OK) {
            return status;
        }
    }

    if (!apply_before_lut) {
        applyToneAdjustments(image, local);
    }
    applyDetailAdjustments(image, local);

    uint32_t target_width = static_cast<uint32_t>(image.width);
    uint32_t target_height = static_cast<uint32_t>(image.height);
    status = computeTargetSize(local, target_width, target_height, &target_width, &target_height);
    if (status != SONY2FUJI_STATUS_OK) {
        return status;
    }

    if (target_width != static_cast<uint32_t>(image.width) ||
        target_height != static_cast<uint32_t>(image.height)) {
        image = resizeBilinear(image, target_width, target_height);
    }

    if (local.output_target == SONY2FUJI_TARGET_FILE) {
        return writeOutputFile(local, image);
    }

    return writeOutputBuffer(local, image, out_buffer);
}

sony2fuji_status sony2fuji_compute_histogram(
    const sony2fuji_buffer* buffer,
    uint32_t bins,
    float* out_bins
) {
    if (!buffer || !buffer->data || !out_bins || bins == 0) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }
    if (buffer->width == 0 || buffer->height == 0 || buffer->stride_bytes == 0) {
        return SONY2FUJI_STATUS_INVALID_ARGUMENT;
    }
    if (buffer->pixel_format != SONY2FUJI_PIXEL_RGB8 &&
        buffer->pixel_format != SONY2FUJI_PIXEL_RGBA8) {
        return SONY2FUJI_STATUS_UNSUPPORTED;
    }

    uint32_t channels = buffer->pixel_format == SONY2FUJI_PIXEL_RGBA8 ? 4 : 3;
    const uint8_t* data = static_cast<const uint8_t*>(buffer->data);
    std::vector<uint32_t> counts(bins, 0);

    for (uint32_t y = 0; y < buffer->height; ++y) {
        const uint8_t* row = data + static_cast<size_t>(y) * buffer->stride_bytes;
        for (uint32_t x = 0; x < buffer->width; ++x) {
            size_t offset = static_cast<size_t>(x) * channels;
            float r = row[offset] / 255.0f;
            float g = row[offset + 1] / 255.0f;
            float b = row[offset + 2] / 255.0f;
            float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            uint32_t bin = static_cast<uint32_t>(l * static_cast<float>(bins - 1));
            if (bin >= bins) {
                bin = bins - 1;
            }
            counts[bin] += 1;
        }
    }

    float max_value = 0.0f;
    for (uint32_t i = 0; i < bins; ++i) {
        float value = static_cast<float>(counts[i]);
        out_bins[i] = value;
        if (value > max_value) {
            max_value = value;
        }
    }

    if (max_value > 0.0f) {
        for (uint32_t i = 0; i < bins; ++i) {
            out_bins[i] = out_bins[i] / max_value;
        }
    }

    return SONY2FUJI_STATUS_OK;
}

void sony2fuji_release_buffer(sony2fuji_buffer* buffer) {
    if (!buffer || !buffer->data) {
        return;
    }

    std::free(buffer->data);
    buffer->data = nullptr;
    buffer->size_bytes = 0;
    buffer->width = 0;
    buffer->height = 0;
    buffer->stride_bytes = 0;
    buffer->pixel_format = SONY2FUJI_PIXEL_RGB8;
}

const char* sony2fuji_status_message(sony2fuji_status status) {
    switch (status) {
        case SONY2FUJI_STATUS_OK:
            return "ok";
        case SONY2FUJI_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case SONY2FUJI_STATUS_UNSUPPORTED:
            return "unsupported";
        case SONY2FUJI_STATUS_IO_ERROR:
            return "io error";
        case SONY2FUJI_STATUS_PROCESSING_ERROR:
            return "processing error";
        case SONY2FUJI_STATUS_OUT_OF_MEMORY:
            return "out of memory";
        default:
            return "unknown";
    }
}
