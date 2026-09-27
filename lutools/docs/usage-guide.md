# Sony2Fuji 使用指南

## 快速开始

### 1. 安装依赖

**macOS:**
```bash
brew install cmake libraw pkg-config
```

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install cmake libraw-dev pkg-config build-essential
```

**Windows:**
- 安装 [CMake](https://cmake.org/download/)
- 安装 [Visual Studio](https://visualstudio.microsoft.com/)
- 使用 vcpkg 安装 libraw

### 2. 构建项目

```bash
# 克隆或下载项目
cd lutools

# 构建
./build.sh

# 或手动构建
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 3. 基本使用

```bash
# 查看帮助
./build/sony2fuji --help

# 处理单张图片
./build/sony2fuji input.ARW \
  -l F-Log2/X100VI_FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube \
  -o output.jpg

# 使用自定义质量
./build/sony2fuji input.ARW -l lut.cube -o output.jpg -q 98

# 使用自动白平衡
./build/sony2fuji input.ARW -l lut.cube -o output.jpg --auto-wb
```

## 详细用法

### 命令行参数

**必需参数:**
- `<输入文件>` - Sony RAW 文件路径 (.ARW)
- `-l, --lut <文件>` - 富士 LUT 文件路径 (.cube)
- `-o, --output <文件>` - 输出文件路径 (.jpg 或 .png)

**可选参数:**
- `-q, --quality <1-100>` - JPEG 质量 (默认: 95)
- `--auto-wb` - 使用自动白平衡
- `--camera-wb` - 使用相机白平衡 (默认)
- `--exposure <EV>` - 曝光补偿 (默认: 0.0)
- `--brightness <值>` - 亮度调整 (默认: 1.0)
- `-h, --help` - 显示帮助信息
- `-v, --version` - 显示版本信息

### 使用示例

#### 1. 基本处理

```bash
./sony2fuji DSC00001.ARW \
  -l F-Log2/X100VI_FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube \
  -o output.jpg
```

#### 2. 高质量输出

```bash
./sony2fuji DSC00001.ARW \
  -l F-Log2/X100VI_FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube \
  -o output.jpg \
  -q 100
```

#### 3. PNG 输出 (无损)

```bash
./sony2fuji DSC00001.ARW \
  -l F-Log2/X100VI_FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube \
  -o output.png
```

#### 4. 曝光和白平衡调整

```bash
./sony2fuji DSC00001.ARW \
  -l lut.cube \
  -o output.jpg \
  --auto-wb \
  --exposure 0.5 \
  --brightness 1.1
```

#### 5. 批量处理 (使用 shell 脚本)

```bash
#!/bin/bash
LUT="F-Log2/X100VI_FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube"

for file in *.ARW; do
    output="${file%.ARW}_eterna.jpg"
    echo "处理: $file -> $output"
    ./sony2fuji "$file" -l "$LUT" -o "$output" -q 95
done
```

## 支持的 LUT

项目包含以下富士 F-Log2 LUT:

1. **ETERNA** - 电影胶片风格
   - 文件: `FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube`
   - 适用: 电影/视频制作

2. **ETERNA BLEACH BYPASS** - 漂白风格
   - 文件: `FLog2_FGamut_to_ETERNA-BB_BT.709_33grid_V.1.00.cube`
   - 适用: 高对比度效果

3. **WDR** - 宽动态范围
   - 文件: `FLog2_FGamut_to_WDR_BT.709_33grid_V.1.00.cube`
   - 适用: 保留更多细节

4. **F-Log2 BT.709** - 中性色调
   - 文件: `FLog2_FGamut_to_FLog2_BT.709_33grid_V.1.00.cube`
   - 适用: 后期调色基准

## 工作流程说明

### 处理流程

```
Sony RAW (.ARW)
    ↓
[1] 使用 libraw 解码
    ↓
线性 RGB 数据 (Sony 色彩空间)
    ↓
[2] 色彩空间转换
    ↓
线性 RGB 数据 (Fuji F-Gamut)
    ↓
[3] 应用 3D LUT (三线性插值)
    ↓
非线性 RGB 数据 (BT.709)
    ↓
[4] 编码输出
    ↓
JPEG/PNG 文件
```

### 色彩科学

**Sony 原生色彩空间 → Fuji F-Gamut**
- 使用 3x3 色彩转换矩阵
- 通过 XYZ 色彩空间作为中间媒介
- 保留色彩精度

**3D LUT 应用**
- 33x33x33 查找表 (35,937 个采样点)
- 三线性插值 (平滑过渡)
- 包含 gamma 曲线和色彩映射

## 性能提示

### 处理时间参考

| 图像尺寸 | 处理时间 (参考) |
|---------|----------------|
| 12MP    | ~1-2 秒        |
| 24MP    | ~2-5 秒        |
| 42MP    | ~5-10 秒       |

*测试环境: Intel i7, 16GB RAM*

### 优化建议

1. **批量处理时复用 LUT**
   - LUT 只需加载一次
   - 可以显著提升速度

2. **使用 SSD**
   - RAW 文件 I/O 密集
   - SSD 可以减少加载时间

3. **合理设置 JPEG 质量**
   - 95: 平衡大小和质量 (推荐)
   - 98-100: 接近无损
   - 80-90: 较小文件

4. **预览时降低分辨率**
   - 可以在 libraw 处理时设置
   - 需要修改代码

## 常见问题

### Q: 支持哪些相机?
A: 理论上支持所有 libraw 支持的 Sony 相机,包括:
- Sony Alpha 系列 (A7, A7R, A7S, A9, A1)
- Sony RX 系列

### Q: 可以用在其他品牌相机吗?
A: 可以,但色彩转换需要调整:
- 目前针对 Sony 优化
- 其他品牌需要修改色彩矩阵

### Q: 输出的 JPEG 是什么色彩空间?
A: BT.709 (Rec.709),标准视频/显示色彩空间

### Q: 为什么结果和富士相机不完全一样?
A: 因为:
- Sony 和 Fuji 传感器特性不同
- 原始色彩空间不同
- 本工具尽力模拟,但不可能完全相同

### Q: 可以自定义 LUT 吗?
A: 可以!只要是 .cube 格式的 3D LUT 都可以使用

### Q: 处理 RAW 时保留 EXIF 数据吗?
A: 当前版本不保留,可以使用 exiftool 后期添加

### Q: 内存占用多少?
A: 约为图像像素数 × 12 字节
- 24MP: ~288 MB
- 42MP: ~504 MB

## 技术细节

### 色彩空间矩阵

Sony Native → XYZ (近似值):
```
[0.4125, 0.3576, 0.1804]
[0.2127, 0.7152, 0.0722]
[0.0193, 0.1192, 0.9503]
```

Fuji F-Gamut → XYZ:
```
[0.7161, 0.1009, 0.1472]
[0.2582, 0.7249, 0.0169]
[0.0000, 0.0051, 1.0145]
```

### LUT 格式

.cube 文件格式:
```
# 标题和元数据
LUT_3D_SIZE 33

# RGB 三元组 (33³ = 35,937 行)
0.006836 0.006836 0.006836
...
```

存储顺序: Blue 变化最快,然后 Green,最后 Red

## 故障排除

### 错误: 无法加载 RAW 文件
- 检查文件路径是否正确
- 确认文件格式 (.ARW)
- 验证文件未损坏

### 错误: 加载 LUT 失败
- 检查 LUT 文件路径
- 确认是 .cube 格式
- 验证文件完整性

### 错误: libraw 相关错误
- 确认已安装 libraw
- macOS: `brew install libraw`
- Linux: `sudo apt-get install libraw-dev`

### 构建错误
- 确认已安装 CMake 3.15+
- 确认已安装 C++17 编译器
- 检查依赖库是否正确安装

## 进一步阅读

- [README.md](../README.md) - 项目概览
- [平台集成指南](platform-integration.md) - iOS/Android 集成
- [LUTCalc 项目](https://github.com/cameramanben/LUTCalc) - LUT 参考实现
- [F-Log2 LUT 文档](../F-Log2/F-Log2_LUT_overview_Ver.1.1E.pdf) - 富士官方文档

## 联系和反馈

- GitHub Issues: 报告问题和建议
- 贡献代码: 欢迎 Pull Requests

## 许可证

MIT License - 详见 LICENSE 文件
