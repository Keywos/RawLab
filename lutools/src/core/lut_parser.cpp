#include "sony2fuji/lut_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace sony2fuji {

namespace {

// ============================================================================
// LUT order detection (R-fast vs B-fast).
// ============================================================================
enum class LutOrder {
    BFast,
    RFast
};

int indexForOrder(int r, int g, int b, int size, LutOrder order) {
    if (order == LutOrder::BFast) {
        return (r * size + g) * size + b;
    }
    return (b * size + g) * size + r;
}

float channelValue(const RGB& value, int channel) {
    if (channel == 1) {
        return value.g;
    }
    if (channel == 2) {
        return value.b;
    }
    return value.r;
}

float channelRange(const std::vector<RGB>& data, int size, int channel) {
    float min_v = channelValue(data[0], channel);
    float max_v = min_v;
    for (int i = 1; i < size; ++i) {
        float v = channelValue(data[i], channel);
        min_v = std::min(min_v, v);
        max_v = std::max(max_v, v);
    }
    return max_v - min_v;
}

LutOrder detectOrderByFirstBlock(const std::vector<RGB>& data, int size) {
    if (size < 2 || data.size() < static_cast<size_t>(size)) {
        return LutOrder::BFast;
    }

    float r_range = channelRange(data, size, 0);
    float g_range = channelRange(data, size, 1);
    float b_range = channelRange(data, size, 2);

    if (r_range >= g_range && r_range >= b_range) {
        return LutOrder::RFast;
    }
    if (b_range >= r_range && b_range >= g_range) {
        return LutOrder::BFast;
    }

    return LutOrder::BFast;
}

std::vector<RGB> reorderFromRFastToBFast(const std::vector<RGB>& data, int size) {
    std::vector<RGB> reordered(data.size());
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                int rfast_index = indexForOrder(r, g, b, size, LutOrder::RFast);
                int bfast_index = indexForOrder(r, g, b, size, LutOrder::BFast);
                reordered[bfast_index] = data[rfast_index];
            }
        }
    }
    return reordered;
}

} // namespace

// ============================================================================
// LUT3D Implementation
// ============================================================================

ErrorCode LUT3D::loadFromFile(const std::string& filepath) {
    return parseCubeFile(filepath);
}

RGB LUT3D::getValue(int r, int g, int b) const {
    if (r < 0 || r >= size_ || g < 0 || g >= size_ || b < 0 || b >= size_) {
        return RGB(0, 0, 0);
    }

    // ============================================================================
    // Normalized LUT storage is B-fast (blue changes fastest).
    // index = r * size^2 + g * size + b
    // ============================================================================
    int index = r * size_ * size_ + g * size_ + b;

    if (index >= 0 && index < static_cast<int>(data_.size())) {
        return data_[index];
    }

    return RGB(0, 0, 0);
}

ErrorCode LUT3D::parseCubeFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filepath << std::endl;
        return ErrorCode::FileNotFound;
    }

    std::string line;
    bool sizeFound = false;
    std::vector<RGB> tempData;

    while (std::getline(file, line)) {
        // 去除首尾空白
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // 跳过空行
        if (line.empty()) {
            continue;
        }

        // 解析注释和元数据
        if (line[0] == '#') {
            if (line.find("#title:") == 0 || line.find("#Title:") == 0) {
                title_ = line.substr(7);
            } else if (line.find("#") == 0) {
                // 其他注释,添加到描述
                if (!description_.empty()) {
                    description_ += "\n";
                }
                description_ += line.substr(1);
            }
            continue;
        }

        // 解析 LUT_3D_SIZE
        if (line.find("LUT_3D_SIZE") == 0) {
            std::istringstream iss(line);
            std::string keyword;
            iss >> keyword >> size_;

            if (size_ <= 0 || size_ > 256) {
                std::cerr << "无效的 LUT 大小: " << size_ << std::endl;
                return ErrorCode::InvalidFormat;
            }

            sizeFound = true;
            tempData.reserve(size_ * size_ * size_);
            continue;
        }

        // 解析 RGB 数据
        if (sizeFound) {
            std::istringstream iss(line);
            float r, g, b;

            if (iss >> r >> g >> b) {
                // 确保值在合理范围内 [0, 1]
                r = std::max(0.0f, std::min(1.0f, r));
                g = std::max(0.0f, std::min(1.0f, g));
                b = std::max(0.0f, std::min(1.0f, b));

                tempData.emplace_back(r, g, b);
            } else {
                std::cerr << "解析 RGB 值失败: " << line << std::endl;
            }
        }
    }

    file.close();

    // 验证数据
    if (!sizeFound) {
        std::cerr << "未找到 LUT_3D_SIZE" << std::endl;
        return ErrorCode::ParseError;
    }

    int expectedSize = size_ * size_ * size_;
    if (static_cast<int>(tempData.size()) != expectedSize) {
        std::cerr << "LUT 数据大小不匹配: 期望 " << expectedSize
                  << ", 实际 " << tempData.size() << std::endl;
        return ErrorCode::ParseError;
    }

    // ============================================================================
    // Data validated: normalize to B-fast storage.
    // ============================================================================
    LutOrder order = detectOrderByFirstBlock(tempData, size_);
    if (order == LutOrder::RFast) {
        data_ = reorderFromRFastToBFast(tempData, size_);
    } else {
        data_ = std::move(tempData);
    }

    std::cout << "成功加载 LUT: " << title_ << std::endl;
    std::cout << "  大小: " << size_ << "x" << size_ << "x" << size_ << std::endl;
    std::cout << "  数据点数: " << data_.size() << std::endl;

    return ErrorCode::Success;
}

// ============================================================================
// LUTParser Implementation
// ============================================================================

std::unique_ptr<LUT3D> LUTParser::loadLUT(const std::string& filepath) {
    std::string format = detectFormat(filepath);

    if (format == "cube") {
        auto lut = std::make_unique<LUT3D>();
        ErrorCode result = lut->loadFromFile(filepath);

        if (result == ErrorCode::Success) {
            return lut;
        } else {
            return nullptr;
        }
    }

    std::cerr << "不支持的 LUT 格式: " << filepath << std::endl;
    return nullptr;
}

std::shared_ptr<LUT3D> LUTParser::loadLUTCached(const std::string& filepath) {
    if (filepath.empty()) {
        return nullptr;
    }

    static std::mutex cacheMutex;
    static std::unordered_map<std::string, std::weak_ptr<LUT3D>> cache;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (auto it = cache.find(filepath); it != cache.end()) {
            if (auto cached = it->second.lock()) {
                return cached;
            }
        }
    }

    auto loaded = loadLUT(filepath);
    if (!loaded) {
        return nullptr;
    }

    std::shared_ptr<LUT3D> shared(std::move(loaded));
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache[filepath] = shared;
    }

    return shared;
}

std::string LUTParser::detectFormat(const std::string& filepath) {
    // 通过文件扩展名检测格式
    size_t dotPos = filepath.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string ext = filepath.substr(dotPos + 1);

        // 转换为小写
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == "cube") {
            return "cube";
        } else if (ext == "3dl") {
            return "3dl";  // 未来可以支持
        }
    }

    return "";  // 未知格式
}

} // namespace sony2fuji
