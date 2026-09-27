#include "sony2fuji/raw_processor.h"
#include <libraw/libraw.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <cstring>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _OPENMP
namespace {
constexpr int kParallelThreshold = 1 << 16;
}
#endif

namespace sony2fuji {

// ============================================================================
// RAWProcessor::Impl - pImpl pattern
// ============================================================================

class RAWProcessor::Impl {
public:
    Impl() : rawProcessor_(std::make_unique<LibRaw>()) {}

    ErrorCode loadFile(const std::string& filepath) {
        int ret = rawProcessor_->open_file(filepath.c_str());
        if (ret != LIBRAW_SUCCESS) {
            std::cerr << "无法打开 RAW 文件: " << libraw_strerror(ret) << std::endl;
            return ErrorCode::FileNotFound;
        }

        filepath_ = filepath;
        return ErrorCode::Success;
    }

    ErrorCode process(const RAWProcessOptions& options, ImageData& output) {
        if (filepath_.empty()) {
            return ErrorCode::FileNotFound;
        }

        // 设置处理参数
        rawProcessor_->imgdata.params.use_camera_wb = options.useCameraWhiteBalance ? 1 : 0;
        rawProcessor_->imgdata.params.use_auto_wb = options.useAutoWhiteBalance ? 1 : 0;
        if (options.useCustomWhiteBalance) {
            rawProcessor_->imgdata.params.use_camera_wb = 0;
            rawProcessor_->imgdata.params.use_auto_wb = 0;
            for (int i = 0; i < 4; ++i) {
                rawProcessor_->imgdata.params.user_mul[i] = options.customWhiteBalance[i];
            }
        }
        rawProcessor_->imgdata.params.output_bps = options.outputBitsPerSample;

        const bool outputLinear = options.outputLinear;
        rawProcessor_->imgdata.params.no_auto_bright = 0;
        int outputColor = 1;
        if (outputLinear) {
            if (options.outputAces) {
                outputColor = 6;
            } else if (options.outputAdobe) {
                outputColor = 2;
            } else {
                outputColor = 0;
            }
        }
        rawProcessor_->imgdata.params.output_color = outputColor;
        rawProcessor_->imgdata.params.gamm[0] = outputLinear ? 1.0 : (1.0 / 2.4);
        rawProcessor_->imgdata.params.gamm[1] = outputLinear ? 1.0 : 12.92;
        rawProcessor_->imgdata.params.bright = options.brightness;

        if (options.exposure != 0.0f) {
            float shift = std::pow(2.0f, options.exposure);
            rawProcessor_->imgdata.params.exp_correc = 1;
            rawProcessor_->imgdata.params.exp_shift = std::min(8.0f, std::max(0.25f, shift));
            rawProcessor_->imgdata.params.exp_preser = 0.0f;
        } else {
            rawProcessor_->imgdata.params.exp_correc = 0;
            rawProcessor_->imgdata.params.exp_shift = 1.0f;
            rawProcessor_->imgdata.params.exp_preser = 0.0f;
        }

        // 解包 RAW 数据
        int ret = rawProcessor_->unpack();
        if (ret != LIBRAW_SUCCESS) {
            std::cerr << "解包 RAW 数据失败: " << libraw_strerror(ret) << std::endl;
            return ErrorCode::ProcessingError;
        }

        // 处理图像 (demosaic)
        ret = rawProcessor_->dcraw_process();
        if (ret != LIBRAW_SUCCESS) {
            std::cerr << "处理 RAW 数据失败: " << libraw_strerror(ret) << std::endl;
            return ErrorCode::ProcessingError;
        }

        // 获取处理后的图像
        libraw_processed_image_t* image = rawProcessor_->dcraw_make_mem_image(&ret);
        if (!image) {
            std::cerr << "创建内存图像失败: " << libraw_strerror(ret) << std::endl;
            return ErrorCode::ProcessingError;
        }

        // 转换为 ImageData
        output.width = image->width;
        output.height = image->height;
        output.pixels.resize(output.width * output.height);

        if (image->type == LIBRAW_IMAGE_BITMAP && image->colors == 3) {
            // RGB 数据
            const uint16_t* data = reinterpret_cast<const uint16_t*>(image->data);
            float maxVal = (1 << options.outputBitsPerSample) - 1.0f;

            const int pixel_count = output.width * output.height;
#ifdef _OPENMP
#pragma omp parallel for if (pixel_count >= kParallelThreshold)
#endif
            for (int i = 0; i < pixel_count; ++i) {
                output.pixels[i].r = data[i * 3 + 0] / maxVal;
                output.pixels[i].g = data[i * 3 + 1] / maxVal;
                output.pixels[i].b = data[i * 3 + 2] / maxVal;
            }
        } else {
            std::cerr << "不支持的图像格式" << std::endl;
            LibRaw::dcraw_clear_mem(image);
            return ErrorCode::InvalidFormat;
        }

        LibRaw::dcraw_clear_mem(image);
        return ErrorCode::Success;
    }

    int getWidth() const {
        return rawProcessor_->imgdata.sizes.width;
    }

    int getHeight() const {
        return rawProcessor_->imgdata.sizes.height;
    }

    std::string getCameraMake() const {
        return std::string(rawProcessor_->imgdata.idata.make);
    }

    std::string getCameraModel() const {
        return std::string(rawProcessor_->imgdata.idata.model);
    }

    ColorSpace getNativeColorSpace() const {
        // 简化处理:根据相机制造商返回色彩空间
        std::string make = getCameraMake();
        if (make.find("Sony") != std::string::npos ||
            make.find("SONY") != std::string::npos) {
            return ColorSpace::SonyNative;
        }
        return ColorSpace::sRGB;
    }

    bool getCameraColorMatrix(float matrix[3][3]) const {
        // 从 libraw 获取相机色彩矩阵
        // imgdata.color.cam_xyz 是 Camera RGB -> XYZ 矩阵
        const float (&cam_xyz)[4][3] = rawProcessor_->imgdata.color.cam_xyz;

        // cam_xyz is stored as [channel][xyz], transpose to [xyz][rgb]
        matrix[0][0] = cam_xyz[0][0];
        matrix[0][1] = cam_xyz[1][0];
        matrix[0][2] = cam_xyz[2][0];

        matrix[1][0] = cam_xyz[0][1];
        matrix[1][1] = cam_xyz[1][1];
        matrix[1][2] = cam_xyz[2][1];

        matrix[2][0] = cam_xyz[0][2];
        matrix[2][1] = cam_xyz[1][2];
        matrix[2][2] = cam_xyz[2][2];

        return true;
    }

private:
    std::unique_ptr<LibRaw> rawProcessor_;
    std::string filepath_;
};

// ============================================================================
// RAWProcessor Implementation
// ============================================================================

RAWProcessor::RAWProcessor()
    : pImpl_(std::make_unique<Impl>()) {
}

RAWProcessor::~RAWProcessor() = default;

ErrorCode RAWProcessor::loadFile(const std::string& filepath) {
    return pImpl_->loadFile(filepath);
}

ErrorCode RAWProcessor::process(const RAWProcessOptions& options, ImageData& output) {
    return pImpl_->process(options, output);
}

int RAWProcessor::getWidth() const {
    return pImpl_->getWidth();
}

int RAWProcessor::getHeight() const {
    return pImpl_->getHeight();
}

std::string RAWProcessor::getCameraMake() const {
    return pImpl_->getCameraMake();
}

std::string RAWProcessor::getCameraModel() const {
    return pImpl_->getCameraModel();
}

ColorSpace RAWProcessor::getNativeColorSpace() const {
    return pImpl_->getNativeColorSpace();
}

bool RAWProcessor::getCameraColorMatrix(float matrix[3][3]) const {
    return pImpl_->getCameraColorMatrix(matrix);
}

// ============================================================================
// ImageEncoder Implementation
// ============================================================================

uint8_t ImageEncoder::floatToUint8(float value) {
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

uint16_t ImageEncoder::floatToUint16(float value) {
    value = std::max(0.0f, std::min(1.0f, value));
    return static_cast<uint16_t>(value * 65535.0f + 0.5f);
}

ErrorCode ImageEncoder::saveImage(
    const ImageData& image,
    const std::string& filepath,
    OutputFormat format,
    int quality
) {
    switch (format) {
        case OutputFormat::JPEG:
            return saveJPEG(image, filepath, quality);
        case OutputFormat::PNG:
            return savePNG(image, filepath);
        default:
            return ErrorCode::InvalidFormat;
    }
}

ErrorCode ImageEncoder::saveJPEG(
    const ImageData& image,
    const std::string& filepath,
    int quality
) {
    if (image.pixels.empty()) {
        return ErrorCode::ProcessingError;
    }

    // 转换为 uint8_t
    std::vector<uint8_t> data(image.width * image.height * 3);

    const int pixel_count = image.width * image.height;
#ifdef _OPENMP
#pragma omp parallel for if (pixel_count >= kParallelThreshold)
#endif
    for (int i = 0; i < pixel_count; ++i) {
        data[i * 3 + 0] = floatToUint8(image.pixels[i].r);
        data[i * 3 + 1] = floatToUint8(image.pixels[i].g);
        data[i * 3 + 2] = floatToUint8(image.pixels[i].b);
    }

    int result = stbi_write_jpg(
        filepath.c_str(),
        image.width,
        image.height,
        3,
        data.data(),
        quality
    );

    if (result == 0) {
        std::cerr << "保存 JPEG 失败: " << filepath << std::endl;
        return ErrorCode::ProcessingError;
    }

    std::cout << "成功保存: " << filepath << std::endl;
    return ErrorCode::Success;
}

ErrorCode ImageEncoder::savePNG(
    const ImageData& image,
    const std::string& filepath
) {
    if (image.pixels.empty()) {
        return ErrorCode::ProcessingError;
    }

    // 转换为 uint16_t (16-bit PNG)
    std::vector<uint16_t> data(image.width * image.height * 3);

    const int pixel_count = image.width * image.height;
#ifdef _OPENMP
#pragma omp parallel for if (pixel_count >= kParallelThreshold)
#endif
    for (int i = 0; i < pixel_count; ++i) {
        data[i * 3 + 0] = floatToUint16(image.pixels[i].r);
        data[i * 3 + 1] = floatToUint16(image.pixels[i].g);
        data[i * 3 + 2] = floatToUint16(image.pixels[i].b);
    }

    int result = stbi_write_png(
        filepath.c_str(),
        image.width,
        image.height,
        3,
        data.data(),
        image.width * 3 * sizeof(uint16_t)
    );

    if (result == 0) {
        std::cerr << "保存 PNG 失败: " << filepath << std::endl;
        return ErrorCode::ProcessingError;
    }

    std::cout << "成功保存: " << filepath << std::endl;
    return ErrorCode::Success;
}

} // namespace sony2fuji
