#include "sony2fuji/sony2fuji.h"
#include <iostream>

using namespace sony2fuji;

/**
 * 简单示例: 加载 RAW 文件并应用富士 LUT
 */
int main() {
    try {
        std::cout << "Sony to Fuji LUT Tool - 简单示例\n\n";

        // 1. 创建 RAW 处理器
        RAWProcessor processor;

        // 2. 加载 RAW 文件
        std::cout << "加载 RAW 文件...\n";
        ErrorCode result = processor.loadFile("test.ARW");

        if (result != ErrorCode::Success) {
            std::cerr << "错误: 无法加载 RAW 文件\n";
            return 1;
        }

        std::cout << "相机: " << processor.getCameraMake()
                  << " " << processor.getCameraModel() << "\n";
        std::cout << "尺寸: " << processor.getWidth()
                  << " x " << processor.getHeight() << "\n\n";

        // 3. 处理 RAW 数据
        std::cout << "处理 RAW 数据...\n";
        RAWProcessOptions options;
        options.useCameraWhiteBalance = true;
        options.outputLinear = true;

        ImageData image;
        result = processor.process(options, image);

        if (result != ErrorCode::Success) {
            std::cerr << "错误: 处理 RAW 数据失败\n";
            return 1;
        }

        std::cout << "图像数据: " << image.width << "x" << image.height
                  << ", " << image.pixels.size() << " 像素\n\n";

        // 4. 色彩空间转换
        std::cout << "色彩空间转换 (Sony → Fuji F-Gamut)...\n";
        ColorConverter converter;
        result = converter.convertImage(
            image,
            processor.getNativeColorSpace(),
            ColorSpace::FujiFilm_FGamut
        );

        if (result != ErrorCode::Success) {
            std::cerr << "错误: 色彩空间转换失败\n";
            return 1;
        }

        // 5. 加载 LUT
        std::cout << "加载 LUT...\n";
        auto lut = LUTParser::loadLUT(
            "F-Log2/X100VI_FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube"
        );

        if (!lut || !lut->isValid()) {
            std::cerr << "错误: 加载 LUT 失败\n";
            return 1;
        }

        std::cout << "LUT: " << lut->getTitle() << "\n";
        std::cout << "大小: " << lut->getSize() << "x"
                  << lut->getSize() << "x" << lut->getSize() << "\n\n";

        // 6. 应用 LUT
        std::cout << "应用 LUT...\n";
        LUTApplicator applicator(std::shared_ptr<LUT3D>(std::move(lut)));
        result = applicator.applyToImage(image);

        if (result != ErrorCode::Success) {
            std::cerr << "错误: 应用 LUT 失败\n";
            return 1;
        }

        // 7. 保存结果
        std::cout << "保存结果...\n";
        result = ImageEncoder::saveJPEG(image, "output_example.jpg", 95);

        if (result != ErrorCode::Success) {
            std::cerr << "错误: 保存图像失败\n";
            return 1;
        }

        std::cout << "\n成功! 输出文件: output_example.jpg\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "异常: " << e.what() << "\n";
        return 1;
    }
}
