#!/bin/bash

# Sony2Fuji 快速测试脚本

set -e

echo "======================================"
echo "Sony2Fuji 工具测试"
echo "======================================"
echo ""

# 检查是否构建成功
if [ ! -f "build/sony2fuji" ]; then
    echo "错误: 未找到 build/sony2fuji"
    echo "请先运行: ./build.sh"
    exit 1
fi

echo "✓ 找到可执行文件: build/sony2fuji"
echo ""

# 测试版本信息
echo "1. 测试版本信息:"
./build/sony2fuji --version
echo ""

# 测试帮助信息
echo "2. 测试帮助信息:"
./build/sony2fuji --help | head -15
echo "   ... (省略部分输出)"
echo ""

# 检查 LUT 文件
echo "3. 检查富士 LUT 文件:"
LUT_COUNT=$(ls F-Log2/*.cube 2>/dev/null | wc -l)
if [ "$LUT_COUNT" -gt 0 ]; then
    echo "   找到 $LUT_COUNT 个 LUT 文件:"
    ls -1 F-Log2/*.cube | while read file; do
        echo "   - $(basename "$file")"
    done
else
    echo "   ⚠️  未找到 LUT 文件"
fi
echo ""

# 说明下一步
echo "4. 使用说明:"
echo ""
echo "准备一个 Sony RAW 文件 (如 DSC00001.ARW)，然后运行:"
echo ""
echo "   ./build/sony2fuji DSC00001.ARW \\"
echo "     -l F-Log2/X100VI_FLog2_FGamut_to_ETERNA_BT.709_33grid_V.1.00.cube \\"
echo "     -o output.jpg"
echo ""
echo "或者使用任何其他 LUT:"
echo ""
for lut in F-Log2/*.cube; do
    if [ -f "$lut" ]; then
        echo "   ./build/sony2fuji input.ARW -l \"$lut\" -o output.jpg"
        break
    fi
done
echo ""

# 检查是否有 RAW 文件可以测试
echo "5. 检查测试文件:"
RAW_FILES=$(find . -maxdepth 2 -name "*.ARW" -o -name "*.arw" 2>/dev/null | wc -l)
if [ "$RAW_FILES" -gt 0 ]; then
    echo "   ✓ 找到 $RAW_FILES 个 Sony RAW 文件"
    find . -maxdepth 2 -name "*.ARW" -o -name "*.arw" 2>/dev/null | head -3 | while read file; do
        echo "     - $file"
    done
    echo ""
    echo "   可以尝试处理这些文件进行测试！"
else
    echo "   ℹ️  未找到 Sony RAW 文件 (.ARW)"
    echo "   请准备一些 Sony 相机的 RAW 文件进行测试"
fi
echo ""

echo "======================================"
echo "✅ 工具已成功构建并可以使用"
echo "======================================"
