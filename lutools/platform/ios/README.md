# iOS 集成指南

将 Sony2Fuji 核心库集成到 iOS 应用程序中。

## 1. 构建 iOS Framework

### 1.1 使用 CMake 构建

```bash
# 配置 iOS 构建
mkdir build-ios && cd build-ios

cmake .. \
  -GXcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_OSX_ARCHITECTURES="arm64" \
  -DBUILD_SHARED_LIB=ON \
  -DBUILD_CLI=OFF \
  -DIOS=ON

# 构建
cmake --build . --config Release
```

### 1.2 输出产物

构建完成后,你会得到:
- `libsony2fuji.dylib` - 动态库
- 或 `sony2fuji.framework` - iOS Framework (推荐)

### 1.3 创建 XCFramework (推荐)

如果你已有 device + simulator framework,可以创建 XCFramework:

```bash
xcodebuild -create-xcframework \
  -framework build-ios-iphoneos-arm64/Release-iphoneos/sony2fuji.framework \
  -framework build-ios-iphonesimulator-x86_64/Release-iphonesimulator/sony2fuji.framework \
  -output build-ios/sony2fuji.xcframework
```

## 2. 添加到 Xcode 项目

### 2.1 添加 Framework

1. 将 `sony2fuji.framework` 或 `sony2fuji.xcframework` 拖到 Xcode 项目中
2. 在项目设置中,选择你的 Target
3. 在 "General" 标签下,将 Framework 添加到 "Frameworks, Libraries, and Embedded Content"
4. 确保设置为 "Embed & Sign"

### 2.2 添加头文件搜索路径

在 Build Settings 中添加:
```
HEADER_SEARCH_PATHS = $(PROJECT_DIR)/path/to/sony2fuji/include
```

## 4. C API 文档

更轻量的 Swift/ObjC 接入建议使用 C FFI,详见:

- `docs/ios-api.md`

## GPU 加速 (Metal)

C API:

```c
sony2fuji_gpu_config gpu_config = {
    .version = SONY2FUJI_GPU_CONFIG_VERSION,
    .struct_size = sizeof(sony2fuji_gpu_config),
    .mode = SONY2FUJI_GPU_AUTO
};
sony2fuji_session_set_gpu_config(session, &gpu_config);
```

C++:

```cpp
sony2fuji::GpuConfig config;
config.mode = sony2fuji::GpuMode::Auto;
sony2fuji::applyLUTWithConfig(lut, *imageData, config);
```

## 3. Swift 封装

创建一个 Swift wrapper 来简化 C++ 接口的使用。

### 3.1 创建 Objective-C++ 桥接文件

**Sony2FujiBridge.h**
```objc
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface Sony2FujiProcessor : NSObject

- (instancetype)init;
- (BOOL)loadRAWFile:(NSString *)filepath error:(NSError **)error;
- (BOOL)applyLUT:(NSString *)lutPath error:(NSError **)error;
- (BOOL)saveImage:(NSString *)outputPath
          quality:(NSInteger)quality
            error:(NSError **)error;

@property (nonatomic, readonly) NSInteger width;
@property (nonatomic, readonly) NSInteger height;
@property (nonatomic, copy, readonly) NSString *cameraMake;
@property (nonatomic, copy, readonly) NSString *cameraModel;

@end

NS_ASSUME_NONNULL_END
```

**Sony2FujiBridge.mm** (Objective-C++)
```objc
#import "Sony2FujiBridge.h"
#include "sony2fuji/sony2fuji.h"
#include <memory>

using namespace sony2fuji;

@interface Sony2FujiProcessor() {
    std::unique_ptr<RAWProcessor> _processor;
    std::unique_ptr<ImageData> _imageData;
    std::shared_ptr<LUT3D> _lut;
}
@end

@implementation Sony2FujiProcessor

- (instancetype)init {
    if (self = [super init]) {
        _processor = std::make_unique<RAWProcessor>();
        _imageData = std::make_unique<ImageData>();
    }
    return self;
}

- (BOOL)loadRAWFile:(NSString *)filepath error:(NSError **)error {
    ErrorCode result = _processor->loadFile(filepath.UTF8String);

    if (result == ErrorCode::Success) {
        RAWProcessOptions options;
        options.useCameraWhiteBalance = true;
        options.outputLinear = true;

        result = _processor->process(options, *_imageData);
    }

    if (result != ErrorCode::Success) {
        if (error) {
            *error = [NSError errorWithDomain:@"Sony2Fuji"
                                        code:(NSInteger)result
                                    userInfo:@{NSLocalizedDescriptionKey: @"Failed to load RAW file"}];
        }
        return NO;
    }

    return YES;
}

- (BOOL)applyLUT:(NSString *)lutPath error:(NSError **)error {
    auto lut = LUTParser::loadLUT(lutPath.UTF8String);

    if (!lut || !lut->isValid()) {
        if (error) {
            *error = [NSError errorWithDomain:@"Sony2Fuji"
                                        code:-1
                                    userInfo:@{NSLocalizedDescriptionKey: @"Failed to load LUT"}];
        }
        return NO;
    }

    _lut = std::shared_ptr<LUT3D>(std::move(lut));

    // 色彩空间转换
    ColorConverter converter;
    converter.convertImage(*_imageData,
                          _processor->getNativeColorSpace(),
                          ColorSpace::FujiFilm_FGamut);

    // 应用 LUT
    LUTApplicator applicator(_lut);
    ErrorCode result = applicator.applyToImage(*_imageData);

    if (result != ErrorCode::Success) {
        if (error) {
            *error = [NSError errorWithDomain:@"Sony2Fuji"
                                        code:(NSInteger)result
                                    userInfo:@{NSLocalizedDescriptionKey: @"Failed to apply LUT"}];
        }
        return NO;
    }

    return YES;
}

- (BOOL)saveImage:(NSString *)outputPath
          quality:(NSInteger)quality
            error:(NSError **)error {

    OutputFormat format = OutputFormat::JPEG;
    if ([outputPath.pathExtension.lowercaseString isEqualToString:@"png"]) {
        format = OutputFormat::PNG;
    }

    ErrorCode result = ImageEncoder::saveImage(*_imageData,
                                               outputPath.UTF8String,
                                               format,
                                               (int)quality);

    if (result != ErrorCode::Success) {
        if (error) {
            *error = [NSError errorWithDomain:@"Sony2Fuji"
                                        code:(NSInteger)result
                                    userInfo:@{NSLocalizedDescriptionKey: @"Failed to save image"}];
        }
        return NO;
    }

    return YES;
}

- (NSInteger)width {
    return _processor->getWidth();
}

- (NSInteger)height {
    return _processor->getHeight();
}

- (NSString *)cameraMake {
    return @(_processor->getCameraMake().c_str());
}

- (NSString *)cameraModel {
    return @(_processor->getCameraModel().c_str());
}

@end
```

### 3.2 创建 Swift 接口

**Sony2FujiSwift.swift**
```swift
import Foundation
import UIKit

public class Sony2Fuji {
    private let processor: Sony2FujiProcessor

    public init() {
        processor = Sony2FujiProcessor()
    }

    public func processImage(rawPath: String,
                            lutPath: String,
                            outputPath: String,
                            quality: Int = 95) throws {
        // 加载 RAW 文件
        try processor.loadRAWFile(rawPath)

        print("Loaded RAW: \(processor.cameraMake) \(processor.cameraModel)")
        print("Size: \(processor.width) x \(processor.height)")

        // 应用 LUT
        try processor.applyLUT(lutPath)

        // 保存结果
        try processor.saveImage(outputPath, quality: quality)
    }

    public func processImageAsync(rawPath: String,
                                  lutPath: String,
                                  outputPath: String,
                                  quality: Int = 95,
                                  progress: @escaping (String) -> Void,
                                  completion: @escaping (Result<String, Error>) -> Void) {
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self = self else { return }

            do {
                progress("加载 RAW 文件...")
                try self.processor.loadRAWFile(rawPath)

                progress("应用 LUT...")
                try self.processor.applyLUT(lutPath)

                progress("保存结果...")
                try self.processor.saveImage(outputPath, quality: quality)

                DispatchQueue.main.async {
                    completion(.success(outputPath))
                }
            } catch {
                DispatchQueue.main.async {
                    completion(.failure(error))
                }
            }
        }
    }
}
```

## 4. 使用示例

### 4.1 基本使用

```swift
import UIKit

class ViewController: UIViewController {
    func processImage() {
        let sony2fuji = Sony2Fuji()

        let rawPath = Bundle.main.path(forResource: "DSC00001", ofType: "ARW")!
        let lutPath = Bundle.main.path(forResource: "ETERNA_BT709", ofType: "cube")!
        let outputPath = FileManager.default.temporaryDirectory
            .appendingPathComponent("output.jpg")
            .path

        do {
            try sony2fuji.processImage(
                rawPath: rawPath,
                lutPath: lutPath,
                outputPath: outputPath,
                quality: 95
            )

            // 显示结果
            if let image = UIImage(contentsOfFile: outputPath) {
                imageView.image = image
            }
        } catch {
            print("Error: \(error)")
        }
    }
}
```

### 4.2 异步处理

```swift
func processImageWithProgress() {
    let sony2fuji = Sony2Fuji()

    sony2fuji.processImageAsync(
        rawPath: rawPath,
        lutPath: lutPath,
        outputPath: outputPath,
        progress: { status in
            DispatchQueue.main.async {
                self.progressLabel.text = status
            }
        },
        completion: { result in
            switch result {
            case .success(let path):
                print("Success: \(path)")
                if let image = UIImage(contentsOfFile: path) {
                    self.imageView.image = image
                }
            case .failure(let error):
                print("Error: \(error)")
            }
        }
    )
}
```

## 5. 性能优化

### 5.1 后台处理

建议在后台线程处理图像:

```swift
DispatchQueue.global(qos: .userInitiated).async {
    // 处理图像
    DispatchQueue.main.async {
        // 更新 UI
    }
}
```

### 5.2 内存管理

处理大型 RAW 文件时注意内存管理:

```swift
autoreleasepool {
    try sony2fuji.processImage(...)
}
```

## 6. 常见问题

### Q: Framework 加载失败
A: 确保在 Build Settings 中设置正确的 Library Search Paths 和 Header Search Paths

### Q: 符号未找到错误
A: 确保链接了所有依赖库 (libraw, 等)

### Q: 性能问题
A: 使用后台队列处理,并考虑降低输出分辨率或质量

## 7. 依赖项

iOS Framework 需要以下依赖:
- libraw (需要单独构建 iOS 版本)
- 标准 C++ 库

构建说明见主 README。
