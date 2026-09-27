# Sony to Fuji LUT Tool

将 Sony 相机的 RAW 文件应用富士 F-Log2 LUT 的跨平台工具。

## 功能特性

- 读取 Sony RAW 文件 (.ARW, .ARQ)
- 解析富士 3D LUT 文件 (.cube 格式)
- 色彩空间转换 (Sony 原生色彩空间 → Fuji F-Gamut)
- 应用富士胶片模拟 LUT (ETERNA, ETERNA-BB, WDR 等)
- 输出高质量图像 (JPEG, PNG)
- 跨平台核心库,支持 iOS/Android 集成
- GPU 加速 (iOS Metal / Android OpenGL ES)

## 技术架构

```
┌─────────────────────────────────────┐
│   Platform Layer (iOS/Android)      │
│   Swift/Kotlin + Native Bindings    │
├─────────────────────────────────────┤
│   C++ Core Library                  │
│   ├─ RAW Processing (libraw)        │
│   ├─ LUT Parser (.cube)             │
│   ├─ Color Space Conversion         │
│   ├─ 3D LUT Application             │
│   └─ Image Encoding (JPEG/PNG)      │
└─────────────────────────────────────┘
```

## 项目结构

```
.
├── src/
│   ├── core/           # 核心库代码
│   │   ├── lut_parser.cpp
│   │   ├── lut_applicator.cpp
│   │   ├── color_converter.cpp
│   │   └── raw_processor.cpp
│   └── cli/            # 命令行工具
│       └── main.cpp
├── include/sony2fuji/  # 公共头文件
├── platform/           # 平台特定代码
│   ├── ios/           # iOS wrapper
│   └── android/       # Android JNI wrapper
├── examples/          # 示例代码
└── docs/             # 文档

```

## 构建依赖

- CMake 3.15+
- C++17 编译器
- libraw (RAW 文件处理)
- stb_image_write (图像输出)
- OpenMP (可选, CPU 多核加速)

## 快速开始

### 构建命令行工具

```bash
mkdir build && cd build
cmake ..
make
```

### 使用示例

```bash
# 应用富士 ETERNA LUT
./sony2fuji input.ARW -l F-Log2/ETERNA_BT709.cube -o output.jpg

# 批量处理
./sony2fuji *.ARW -l F-Log2/ETERNA_BT709.cube -o output/
```

## 支持的 Sony 相机

- Sony Alpha 系列 (A7, A7R, A7S, A9 等)
- Sony RX 系列
- 其他支持 ARW 格式的 Sony 相机

## 支持的富士 LUT

- FLog2_FGamut_to_ETERNA_BT.709
- FLog2_FGamut_to_ETERNA-BB_BT.709
- FLog2_FGamut_to_WDR_BT.709
- FLog2_FGamut_to_FLog2_BT.709

## 移动端集成

详见 [平台集成文档](docs/platform-integration.md)

- [iOS 集成指南](platform/ios/README.md)
- [Android 集成指南](platform/android/README.md)

## 技术原理

1. **RAW 解码**: 使用 libraw 将 Sony RAW 文件解码为线性 RGB
2. **色彩空间转换**: 通过矩阵变换将 Sony 原生色彩空间转换到 F-Gamut
3. **LUT 应用**: 使用三线性插值在 3D LUT 中查找映射值
4. **输出编码**: 将处理后的图像编码为 JPEG/PNG

## 参考资料

- [Fuji F-Log2 LUT Overview](F-Log2/F-Log2_LUT_overview_Ver.1.1E.pdf)
- [LUTCalc](https://github.com/cameramanben/LUTCalc) - LUT 处理参考实现

## License

MIT License

## 致谢

- libraw - RAW 文件处理
- stb_image - 图像编解码
- LUTCalc - LUT 算法参考
