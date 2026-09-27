# Android 集成指南

将 Sony2Fuji 核心库集成到 Android 应用程序中。

## 1. 构建 Android 库

### 1.1 使用 CMake 和 NDK 构建

创建 `android-build.sh`:

```bash
#!/bin/bash

# 设置 NDK 路径
export ANDROID_NDK=$HOME/Android/Sdk/ndk/25.2.9519653

# 设置架构
ANDROID_ABI=arm64-v8a  # 或 armeabi-v7a, x86, x86_64

mkdir -p build-android-$ANDROID_ABI
cd build-android-$ANDROID_ABI

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=$ANDROID_ABI \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_shared \
  -DBUILD_SHARED_LIB=ON \
  -DBUILD_CLI=OFF

cmake --build . --config Release

cd ..
```

### 1.2 构建所有架构

```bash
# 构建多个架构
for ABI in arm64-v8a armeabi-v7a x86 x86_64; do
    ./android-build.sh $ABI
done
```

## 2. Android Studio 集成

### 2.1 项目结构

```
app/
├── src/
│   ├── main/
│   │   ├── java/com/yourapp/
│   │   │   └── Sony2FujiProcessor.kt
│   │   ├── cpp/
│   │   │   ├── sony2fuji-jni.cpp
│   │   │   └── CMakeLists.txt
│   │   └── jniLibs/
│   │       ├── arm64-v8a/
│   │       │   └── libsony2fuji.so
│   │       ├── armeabi-v7a/
│   │       │   └── libsony2fuji.so
│   │       ├── x86/
│   │       │   └── libsony2fuji.so
│   │       └── x86_64/
│   │           └── libsony2fuji.so
│   └── androidTest/
└── build.gradle
```

### 2.2 app/build.gradle

```gradle
android {
    compileSdk 34

    defaultConfig {
        applicationId "com.yourapp.sony2fuji"
        minSdk 24
        targetSdk 34

        ndk {
            abiFilters 'arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'
        }

        externalNativeBuild {
            cmake {
                cppFlags "-std=c++17"
                arguments "-DANDROID_STL=c++_shared"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path "src/main/cpp/CMakeLists.txt"
            version "3.22.1"
        }
    }
}

dependencies {
    implementation 'androidx.core:core-ktx:1.12.0'
    implementation 'org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3'
}
```

### 2.3 CMakeLists.txt (JNI)

```cmake
cmake_minimum_required(VERSION 3.22.1)
project(sony2fuji-jni)

set(CMAKE_CXX_STANDARD 17)

# 添加预构建的 sony2fuji 库
add_library(sony2fuji SHARED IMPORTED)
set_target_properties(sony2fuji PROPERTIES IMPORTED_LOCATION
    ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libsony2fuji.so)

# JNI wrapper
add_library(sony2fuji-jni SHARED
    sony2fuji-jni.cpp
)

target_include_directories(sony2fuji-jni PRIVATE
    ${CMAKE_SOURCE_DIR}/../../../../include
)

target_link_libraries(sony2fuji-jni
    sony2fuji
    android
    log
)
```

## 3. JNI 封装

### 3.1 sony2fuji-jni.cpp

```cpp
#include <jni.h>
#include <string>
#include <android/log.h>
#include "sony2fuji/sony2fuji.h"

#define LOG_TAG "Sony2Fuji"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace sony2fuji;

// 全局对象 (简化处理,实际应用中应使用更好的生命周期管理)
static std::unique_ptr<RAWProcessor> g_processor;
static std::unique_ptr<ImageData> g_imageData;
static std::shared_ptr<LUT3D> g_lut;

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_yourapp_Sony2FujiProcessor_nativeLoadRAW(
    JNIEnv *env,
    jobject /* this */,
    jstring filepath) {

    const char *path = env->GetStringUTFChars(filepath, nullptr);

    g_processor = std::make_unique<RAWProcessor>();
    g_imageData = std::make_unique<ImageData>();

    ErrorCode result = g_processor->loadFile(path);
    env->ReleaseStringUTFChars(filepath, path);

    if (result != ErrorCode::Success) {
        LOGE("Failed to load RAW file");
        return JNI_FALSE;
    }

    RAWProcessOptions options;
    options.useCameraWhiteBalance = true;
    options.outputLinear = true;

    result = g_processor->process(options, *g_imageData);

    if (result != ErrorCode::Success) {
        LOGE("Failed to process RAW file");
        return JNI_FALSE;
    }

    LOGI("RAW loaded: %dx%d", g_imageData->width, g_imageData->height);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_yourapp_Sony2FujiProcessor_nativeApplyLUT(
    JNIEnv *env,
    jobject /* this */,
    jstring lutPath) {

    const char *path = env->GetStringUTFChars(lutPath, nullptr);

    auto lut = LUTParser::loadLUT(path);
    env->ReleaseStringUTFChars(lutPath, path);

    if (!lut || !lut->isValid()) {
        LOGE("Failed to load LUT");
        return JNI_FALSE;
    }

    g_lut = std::shared_ptr<LUT3D>(std::move(lut));

    // 色彩空间转换
    ColorConverter converter;
    converter.convertImage(*g_imageData,
                          g_processor->getNativeColorSpace(),
                          ColorSpace::FujiFilm_FGamut);

    // 应用 LUT
    LUTApplicator applicator(g_lut);
    ErrorCode result = applicator.applyToImage(*g_imageData);

    if (result != ErrorCode::Success) {
        LOGE("Failed to apply LUT");
        return JNI_FALSE;
    }

    LOGI("LUT applied successfully");
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_yourapp_Sony2FujiProcessor_nativeSaveImage(
    JNIEnv *env,
    jobject /* this */,
    jstring outputPath,
    jint quality) {

    const char *path = env->GetStringUTFChars(outputPath, nullptr);
    std::string pathStr(path);
    env->ReleaseStringUTFChars(outputPath, path);

    OutputFormat format = OutputFormat::JPEG;
    if (pathStr.find(".png") != std::string::npos) {
        format = OutputFormat::PNG;
    }

    ErrorCode result = ImageEncoder::saveImage(*g_imageData,
                                               pathStr,
                                               format,
                                               quality);

    if (result != ErrorCode::Success) {
        LOGE("Failed to save image");
        return JNI_FALSE;
    }

    LOGI("Image saved: %s", pathStr.c_str());
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_yourapp_Sony2FujiProcessor_nativeGetWidth(
    JNIEnv * /* env */,
    jobject /* this */) {
    return g_processor ? g_processor->getWidth() : 0;
}

JNIEXPORT jint JNICALL
Java_com_yourapp_Sony2FujiProcessor_nativeGetHeight(
    JNIEnv * /* env */,
    jobject /* this */) {
    return g_processor ? g_processor->getHeight() : 0;
}

JNIEXPORT jstring JNICALL
Java_com_yourapp_Sony2FujiProcessor_nativeGetCameraMake(
    JNIEnv *env,
    jobject /* this */) {
    if (!g_processor) return env->NewStringUTF("");
    return env->NewStringUTF(g_processor->getCameraMake().c_str());
}

JNIEXPORT jstring JNICALL
Java_com_yourapp_Sony2FujiProcessor_nativeGetCameraModel(
    JNIEnv *env,
    jobject /* this */) {
    if (!g_processor) return env->NewStringUTF("");
    return env->NewStringUTF(g_processor->getCameraModel().c_str());
}

} // extern "C"
```

## GPU 加速 (OpenGL ES 3.1)

C++/C API 会在 GLES 3.1 不可用时自动回退到 CPU。

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

### 3.2 Kotlin 封装

**Sony2FujiProcessor.kt**

```kotlin
package com.yourapp

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

class Sony2FujiProcessor {

    init {
        System.loadLibrary("sony2fuji-jni")
    }

    // Native 方法声明
    private external fun nativeLoadRAW(filepath: String): Boolean
    private external fun nativeApplyLUT(lutPath: String): Boolean
    private external fun nativeSaveImage(outputPath: String, quality: Int): Boolean
    private external fun nativeGetWidth(): Int
    private external fun nativeGetHeight(): Int
    private external fun nativeGetCameraMake(): String
    private external fun nativeGetCameraModel(): String

    val width: Int get() = nativeGetWidth()
    val height: Int get() = nativeGetHeight()
    val cameraMake: String get() = nativeGetCameraMake()
    val cameraModel: String get() = nativeGetCameraModel()

    /**
     * 处理图像 (同步)
     */
    fun processImage(
        rawPath: String,
        lutPath: String,
        outputPath: String,
        quality: Int = 95
    ): Boolean {
        if (!nativeLoadRAW(rawPath)) {
            return false
        }

        if (!nativeApplyLUT(lutPath)) {
            return false
        }

        return nativeSaveImage(outputPath, quality)
    }

    /**
     * 处理图像 (异步)
     */
    suspend fun processImageAsync(
        rawPath: String,
        lutPath: String,
        outputPath: String,
        quality: Int = 95,
        onProgress: (String) -> Unit = {}
    ): Result<String> = withContext(Dispatchers.IO) {
        try {
            onProgress("加载 RAW 文件...")
            if (!nativeLoadRAW(rawPath)) {
                return@withContext Result.failure(Exception("Failed to load RAW"))
            }

            onProgress("应用 LUT...")
            if (!nativeApplyLUT(lutPath)) {
                return@withContext Result.failure(Exception("Failed to apply LUT"))
            }

            onProgress("保存结果...")
            if (!nativeSaveImage(outputPath, quality)) {
                return@withContext Result.failure(Exception("Failed to save image"))
            }

            Result.success(outputPath)
        } catch (e: Exception) {
            Result.failure(e)
        }
    }
}
```

## 4. 使用示例

### 4.1 Activity 中使用

```kotlin
class MainActivity : AppCompatActivity() {
    private val processor = Sony2FujiProcessor()

    private fun processImage() {
        val rawPath = "/sdcard/DCIM/DSC00001.ARW"
        val lutPath = "${filesDir}/ETERNA_BT709.cube"
        val outputPath = "${filesDir}/output.jpg"

        lifecycleScope.launch {
            processor.processImageAsync(
                rawPath = rawPath,
                lutPath = lutPath,
                outputPath = outputPath,
                quality = 95
            ) { progress ->
                // 更新进度
                runOnUiThread {
                    progressText.text = progress
                }
            }.onSuccess { path ->
                // 显示结果
                val bitmap = BitmapFactory.decodeFile(path)
                imageView.setImageBitmap(bitmap)
            }.onFailure { error ->
                Toast.makeText(this@MainActivity,
                    "Error: ${error.message}",
                    Toast.LENGTH_LONG).show()
            }
        }
    }
}
```

### 4.2 ViewModel 中使用

```kotlin
class ImageProcessViewModel : ViewModel() {
    private val processor = Sony2FujiProcessor()

    private val _processState = MutableLiveData<ProcessState>()
    val processState: LiveData<ProcessState> = _processState

    fun processImage(
        rawPath: String,
        lutPath: String,
        outputPath: String
    ) {
        viewModelScope.launch {
            _processState.value = ProcessState.Loading

            processor.processImageAsync(
                rawPath = rawPath,
                lutPath = lutPath,
                outputPath = outputPath
            ) { progress ->
                _processState.postValue(ProcessState.Progress(progress))
            }.onSuccess { path ->
                _processState.value = ProcessState.Success(path)
            }.onFailure { error ->
                _processState.value = ProcessState.Error(error.message ?: "Unknown error")
            }
        }
    }

    sealed class ProcessState {
        object Loading : ProcessState()
        data class Progress(val message: String) : ProcessState()
        data class Success(val outputPath: String) : ProcessState()
        data class Error(val message: String) : ProcessState()
    }
}
```

## 5. 权限配置

**AndroidManifest.xml**

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />

    <!-- Android 13+ -->
    <uses-permission android:name="android.permission.READ_MEDIA_IMAGES" />

    <application
        android:requestLegacyExternalStorage="true"
        ...>
        ...
    </application>
</manifest>
```

## 6. 性能优化

### 6.1 使用 Kotlin Coroutines

```kotlin
// 在后台线程处理
withContext(Dispatchers.IO) {
    processor.processImage(...)
}
```

### 6.2 内存管理

```kotlin
// 及时释放资源
override fun onDestroy() {
    super.onDestroy()
    // C++ 对象会自动释放
}
```

## 7. 常见问题

### Q: UnsatisfiedLinkError
A: 确保所有架构的 .so 文件都正确放置在 jniLibs 目录

### Q: 找不到 libsony2fuji.so
A: 检查 CMakeLists.txt 中的路径是否正确

### Q: Native crash
A: 使用 Android Studio 的 native debugger 或查看 logcat

## 8. 调试

使用 logcat 查看日志:

```bash
adb logcat | grep Sony2Fuji
```

启用 native debugging:
- Android Studio: Run → Edit Configurations → Debugger → Debug type → Native
