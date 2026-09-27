#include "sony2fuji/sony2fuji.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cctype>

using namespace sony2fuji;

bool isLutDiagnosticsEnabled() {
    const char* value = std::getenv("SONY2FUJI_LUT_DIAG");
    return value != nullptr && value[0] != '\0';
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

void applyFLog2Encoding(ImageData& image, bool clamp_only) {
    GammaConverter::FLog2Options options;
    options.normalizeToRange = !clamp_only;
    options.clampOnly = clamp_only;
    options.enableDiagnostics = isLutDiagnosticsEnabled();

    GammaConverter::FLog2Diagnostics diagnostics = GammaConverter::applyFLog2ToImage(image, options);
    if (options.enableDiagnostics) {
        std::cout << "  F-Log2 diagnostics: min=" << diagnostics.min_linear
                  << " max=" << diagnostics.max_linear
                  << " scale=" << diagnostics.scale
                  << " offset=" << diagnostics.offset
                  << " neg=" << diagnostics.negative_pixels
                  << " over=" << diagnostics.over_pixels << "\n";
    }
}

void printUsage(const char* programName) {
    std::cout << "Sony to Fuji LUT Tool v" << getVersionString() << "\n\n";
    std::cout << "用法:\n";
    std::cout << "  " << programName << " <输入RAW文件> -o <输出文件> [选项]\n\n";
    std::cout << "必需参数:\n";
    std::cout << "  <输入RAW文件>        Sony RAW 文件路径 (.ARW)\n";
    std::cout << "  -o, --output <file>  输出文件路径 (.jpg 或 .png)\n\n";
    std::cout << "可选参数:\n";
    std::cout << "  -l, --lut <file>       富士 LUT 文件路径 (.cube, 可选)\n";
    std::cout << "  --no-lut               不应用 LUT,输出 sRGB 参考图像\n";
    std::cout << "  -q, --quality <1-100>  JPEG 质量 (默认: 95)\n";
    std::cout << "  --auto-wb              使用自动白平衡\n";
    std::cout << "  --camera-wb            使用相机白平衡 (默认)\n";
    std::cout << "  --exposure <EV>        曝光补偿 (默认: 0.0)\n";
    std::cout << "  --brightness <value>   亮度调整 (默认: 1.0)\n";
    std::cout << "  -h, --help             显示此帮助信息\n";
    std::cout << "  -v, --version          显示版本信息\n\n";
    std::cout << "示例:\n";
    std::cout << "  # 不使用 LUT,输出 sRGB 参考图像\n";
    std::cout << "  " << programName << " DSC00001.ARW -o output.jpg --no-lut\n\n";
    std::cout << "  # 使用 ETERNA LUT 处理图像\n";
    std::cout << "  " << programName << " DSC00001.ARW -l F-Log2/ETERNA_BT709.cube -o output.jpg\n\n";
    std::cout << "  # 使用自定义质量和白平衡\n";
    std::cout << "  " << programName << " DSC00001.ARW -l ETERNA.cube -o output.jpg -q 98 --auto-wb\n\n";
}

void printVersion() {
    std::cout << "Sony to Fuji LUT Tool v" << getVersionString() << "\n";
    std::cout << "Copyright (c) 2026\n";
}

struct Options {
    std::string inputFile;
    std::string lutFile;
    std::string outputFile;
    int quality = 95;
    bool autoWhiteBalance = false;
    bool cameraWhiteBalance = true;
    float exposure = 0.0f;
    float brightness = 1.0f;
    bool noLut = false;  // 新增：不应用 LUT
};

bool parseArguments(int argc, char* argv[], Options& options) {
    if (argc < 2) {
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            return false;
        } else if (arg == "-v" || arg == "--version") {
            printVersion();
            exit(0);
        } else if (arg == "-l" || arg == "--lut") {
            if (i + 1 < argc) {
                options.lutFile = argv[++i];
            } else {
                std::cerr << "错误: " << arg << " 需要参数\n";
                return false;
            }
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                options.outputFile = argv[++i];
            } else {
                std::cerr << "错误: " << arg << " 需要参数\n";
                return false;
            }
        } else if (arg == "-q" || arg == "--quality") {
            if (i + 1 < argc) {
                options.quality = std::stoi(argv[++i]);
                if (options.quality < 1 || options.quality > 100) {
                    std::cerr << "错误: 质量必须在 1-100 之间\n";
                    return false;
                }
            } else {
                std::cerr << "错误: " << arg << " 需要参数\n";
                return false;
            }
        } else if (arg == "--auto-wb") {
            options.autoWhiteBalance = true;
            options.cameraWhiteBalance = false;
        } else if (arg == "--camera-wb") {
            options.cameraWhiteBalance = true;
            options.autoWhiteBalance = false;
        } else if (arg == "--exposure") {
            if (i + 1 < argc) {
                options.exposure = std::stof(argv[++i]);
            } else {
                std::cerr << "错误: " << arg << " 需要参数\n";
                return false;
            }
        } else if (arg == "--brightness") {
            if (i + 1 < argc) {
                options.brightness = std::stof(argv[++i]);
            } else {
                std::cerr << "错误: " << arg << " 需要参数\n";
                return false;
            }
        } else if (arg == "--no-lut") {
            options.noLut = true;
        } else if (arg[0] != '-') {
            if (options.inputFile.empty()) {
                options.inputFile = arg;
            } else {
                std::cerr << "错误: 意外的参数 '" << arg << "'\n";
                return false;
            }
        } else {
            std::cerr << "错误: 未知选项 '" << arg << "'\n";
            return false;
        }
    }

    // 验证必需参数
    if (options.inputFile.empty() || options.outputFile.empty()) {
        std::cerr << "错误: 缺少必需参数\n";
        return false;
    }

    // 如果不是 --no-lut 模式,则必须提供 LUT 文件
    if (!options.noLut && options.lutFile.empty()) {
        std::cerr << "错误: 请提供 LUT 文件 (-l) 或使用 --no-lut 直接输出\n";
        return false;
    }

    return true;
}

OutputFormat getOutputFormat(const std::string& filename) {
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string ext = filename.substr(dotPos + 1);
        if (ext == "jpg" || ext == "jpeg" || ext == "JPG" || ext == "JPEG") {
            return OutputFormat::JPEG;
        } else if (ext == "png" || ext == "PNG") {
            return OutputFormat::PNG;
        }
    }
    return OutputFormat::JPEG;  // 默认
}

int main(int argc, char* argv[]) {
    Options options;

    if (!parseArguments(argc, argv, options)) {
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "===========================================\n";
    std::cout << "Sony to Fuji LUT Tool v" << getVersionString() << "\n";
    std::cout << "===========================================\n\n";

    try {
        // 1. 加载 RAW 文件
        std::cout << "[1/5] 加载 RAW 文件...\n";
        std::cout << "  输入: " << options.inputFile << "\n";

        RAWProcessor processor;
        ErrorCode result = processor.loadFile(options.inputFile);
        if (result != ErrorCode::Success) {
            std::cerr << "错误: 无法加载 RAW 文件\n";
            return 1;
        }

        std::cout << "  相机: " << processor.getCameraMake() << " " << processor.getCameraModel() << "\n";
        std::cout << "  尺寸: " << processor.getWidth() << " x " << processor.getHeight() << "\n";

        // 2. 处理 RAW 数据
        std::cout << "\n[2/5] 处理 RAW 数据...\n";

        RAWProcessOptions rawOptions;
        rawOptions.useAutoWhiteBalance = options.autoWhiteBalance;
        rawOptions.useCameraWhiteBalance = options.cameraWhiteBalance;
        rawOptions.exposure = options.exposure;
        rawOptions.brightness = options.brightness;
        rawOptions.outputLinear = !options.noLut;
        AcesConversionMode acesMode = getAcesConversionMode();
        bool useAces = (acesMode != AcesConversionMode::Disabled);
        rawOptions.outputAces = !options.noLut && useAces;
        rawOptions.outputAdobe = !options.noLut && !useAces;

        ImageData image;
        result = processor.process(rawOptions, image);
        if (result != ErrorCode::Success) {
            std::cerr << "错误: 处理 RAW 数据失败\n";
            return 1;
        }

        std::cout << "  白平衡: " << (options.autoWhiteBalance ? "自动" : "相机") << "\n";
        std::cout << "  曝光补偿: " << options.exposure << " EV\n";

        // 3. 色彩空间转换和 LUT 应用 (可选)
        if (!options.noLut) {
            std::cout << "\n[3/5] 色彩空间转换...\n";
            if (rawOptions.outputAces) {
                if (acesMode == AcesConversionMode::AssumeAP1) {
                    std::cout << "  ACEScg (assumed) → Fuji F-Gamut\n";
                } else {
                    std::cout << "  ACES2065-1 → ACEScg → Fuji F-Gamut\n";
                }
            } else {
                std::cout << "  Adobe RGB → Fuji F-Gamut\n";
            }

            ColorConverter converter;
            if (rawOptions.outputAces) {
                if (acesMode == AcesConversionMode::AssumeAP0) {
                    result = converter.convertImage(
                        image,
                        ColorSpace::ACES2065_1,
                        ColorSpace::ACEScg
                    );
                } else {
                    result = ErrorCode::Success;
                }
            }
            if (result != ErrorCode::Success) {
                std::cerr << "错误: 色彩空间转换失败\n";
                return 1;
            }

            if (rawOptions.outputAces) {
                result = converter.convertImage(image,
                                               ColorSpace::ACEScg,
                                               ColorSpace::FujiFilm_FGamut);
            } else {
                result = converter.convertImage(image,
                                               ColorSpace::AdobeRGB,
                                               ColorSpace::FujiFilm_FGamut);
            }
            if (result != ErrorCode::Success) {
                std::cerr << "错误: 色彩空间转换失败\n";
                return 1;
            }

            applyFLog2Encoding(image, !rawOptions.outputAces);

            // 4. 加载和应用 LUT
            std::cout << "\n[4/5] 应用 LUT...\n";
            std::cout << "  LUT 文件: " << options.lutFile << "\n";

            auto lut = LUTParser::loadLUTCached(options.lutFile);
            if (!lut) {
                std::cerr << "错误: 加载 LUT 文件失败\n";
                return 1;
            }

            LUTApplicator applicator(lut);
            result = applicator.applyToImage(image);
            if (result != ErrorCode::Success) {
                std::cerr << "错误: 应用 LUT 失败\n";
                return 1;
            }
        } else {
            std::cout << "\n[3/5] 使用相机色彩矩阵输出 sRGB (--no-lut 模式)\n";
            std::cout << "\n[4/5] 跳过 LUT 应用 (--no-lut 模式)\n";
            std::cout << "  sRGB + gamma + 自动亮度\n";
        }

        // 5. 保存结果
        std::cout << "\n[5/5] 保存结果...\n";
        std::cout << "  输出: " << options.outputFile << "\n";

        OutputFormat format = getOutputFormat(options.outputFile);
        result = ImageEncoder::saveImage(image, options.outputFile, format, options.quality);
        if (result != ErrorCode::Success) {
            std::cerr << "错误: 保存图像失败\n";
            return 1;
        }

        std::cout << "\n===========================================\n";
        std::cout << "处理完成!\n";
        std::cout << "===========================================\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }
}
