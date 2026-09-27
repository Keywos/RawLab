# Sony to Fuji LUT Tool

将 Sony 相机的 RAW 文件应用富士 F-Log2 LUT 的跨平台工具。

新增原生 [RawLab Mac 客户端](../RawLabMac/README.md)。在仓库根目录运行 `bash RawLabMac/build.sh`，然后打开 `build/RawLab Mac.app`。
当前色彩流程与验收边界以 [Photo Rendering Contract](docs/color-contract.md) 为准。

## 功能特性

- 读取 Sony RAW 文件 (.ARW, .ARQ)
- 解析富士 3D LUT 文件 (.cube 格式)
- 浮点相机矩阵转换、线性曝光/白平衡、F-Gamut / F-Log2 编码
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

1. **RAW 解码**: LibRaw 黑电平处理、有效区域裁切、去马赛克，不启用自动亮度或整数 RGB 输出转换。
2. **输入调整**: 浮点相机矩阵、相对白平衡和曝光；保留工作数据中的负值与超白值。
3. **LUT 应用**: 转换到 F-Gamut，按官方 F-Log2 曲线编码，再进行三线性插值。
4. **强度与输出**: 在统一显示 RGB 约定下混合中性图与风格图，输出 JPEG 或真正的 16-bit PNG。

运行 `bash test.sh` 验证数值和实际 RAW；可用 Metal 的 Mac 上设置 `SONY2FUJI_TEST_GPU=1` 验证 GPU/CPU 一致性。Sony ARW 与 DJI DNG 样片验证不等同于所有品牌机型色彩标定。

## 参考资料

- [Fuji F-Log2 LUT Overview](F-Log2/F-Log2_LUT_overview_Ver.1.1E.pdf)
- [LUTCalc](https://github.com/cameramanben/LUTCalc) - LUT 处理参考实现

## License

MIT License

## 致谢

- libraw - RAW 文件处理
- stb_image - 图像编解码
- LUTCalc - LUT 算法参考
