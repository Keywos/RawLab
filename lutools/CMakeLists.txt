cmake_minimum_required(VERSION 3.15)
project(Sony2FujiLUT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 选项
option(BUILD_CLI "Build command line tool" ON)
option(BUILD_SHARED_LIB "Build shared library for mobile integration" ON)
option(BUILD_EXAMPLES "Build examples" OFF)

# 依赖库查找
find_package(PkgConfig)
find_package(ZLIB)

# OpenMP (optional)
set(SONY2FUJI_ENABLE_OPENMP_DEFAULT ON)
if(IOS OR ANDROID)
    set(SONY2FUJI_ENABLE_OPENMP_DEFAULT OFF)
endif()
option(SONY2FUJI_ENABLE_OPENMP "Enable OpenMP for CPU parallelism" ${SONY2FUJI_ENABLE_OPENMP_DEFAULT})
if(SONY2FUJI_ENABLE_OPENMP)
    find_package(OpenMP)
endif()

# GPU acceleration (optional)
option(SONY2FUJI_ENABLE_GPU "Enable GPU acceleration" ON)
if(IOS)
    add_compile_definitions(SONY2FUJI_PLATFORM_IOS)
endif()
if(ANDROID)
    add_compile_definitions(SONY2FUJI_PLATFORM_ANDROID)
endif()

# LibRaw
pkg_check_modules(LIBRAW REQUIRED libraw)
find_library(LIBRAW_LIBRARY NAMES raw HINTS ${LIBRAW_LIBRARY_DIRS} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT LIBRAW_LIBRARY)
    message(FATAL_ERROR "libraw not found in ${LIBRAW_LIBRARY_DIRS}")
endif()

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${LIBRAW_INCLUDE_DIRS}
)

# 核心库源文件
set(CORE_SOURCES
    src/core/lut_parser.cpp
    src/core/lut_applicator.cpp
    src/core/color_converter.cpp
    src/core/raw_processor.cpp
    src/ffi/sony2fuji_c.cpp
    src/gpu/lut_gpu.cpp
)

if(SONY2FUJI_ENABLE_GPU)
    if(IOS)
        list(APPEND CORE_SOURCES src/gpu/metal_lut.mm)
        add_compile_definitions(SONY2FUJI_ENABLE_METAL)
        set_source_files_properties(src/gpu/metal_lut.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    endif()
    if(ANDROID)
        list(APPEND CORE_SOURCES src/gpu/gles_lut.cpp)
        add_compile_definitions(SONY2FUJI_ENABLE_GLES)
    endif()
endif()

# 创建核心库
add_library(sony2fuji_core STATIC ${CORE_SOURCES})
target_link_libraries(sony2fuji_core ${LIBRAW_LIBRARY})
target_compile_options(sony2fuji_core PRIVATE -Wall -Wextra -O3)
if(SONY2FUJI_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(sony2fuji_core OpenMP::OpenMP_CXX)
endif()
if(ZLIB_FOUND)
    target_link_libraries(sony2fuji_core ZLIB::ZLIB)
endif()
if(SONY2FUJI_ENABLE_GPU AND IOS)
    target_link_libraries(sony2fuji_core "-framework Metal" "-framework Foundation")
endif()
if(SONY2FUJI_ENABLE_GPU AND ANDROID)
    target_link_libraries(sony2fuji_core EGL GLESv3)
endif()

# 创建共享库 (用于移动端集成)
if(BUILD_SHARED_LIB)
    add_library(sony2fuji SHARED ${CORE_SOURCES})
    target_link_libraries(sony2fuji ${LIBRAW_LIBRARY})
    target_compile_options(sony2fuji PRIVATE -Wall -Wextra -O3)
    if(SONY2FUJI_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
        target_link_libraries(sony2fuji OpenMP::OpenMP_CXX)
    endif()
    if(ZLIB_FOUND)
        target_link_libraries(sony2fuji ZLIB::ZLIB)
    endif()
    if(SONY2FUJI_ENABLE_GPU AND IOS)
        target_link_libraries(sony2fuji "-framework Metal" "-framework Foundation")
    endif()
    if(SONY2FUJI_ENABLE_GPU AND ANDROID)
        target_link_libraries(sony2fuji EGL GLESv3)
    endif()

    # iOS/Android 特定配置
    if(IOS)
        set_target_properties(sony2fuji PROPERTIES
            FRAMEWORK TRUE
            FRAMEWORK_VERSION A
            MACOSX_FRAMEWORK_IDENTIFIER com.sony2fuji.lib
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.sony2fuji.lib
        )
    endif()
endif()

# 命令行工具
if(BUILD_CLI)
    add_executable(sony2fuji_cli src/cli/main.cpp)
    target_link_libraries(sony2fuji_cli sony2fuji_core)
    set_target_properties(sony2fuji_cli PROPERTIES OUTPUT_NAME sony2fuji)
endif()

# 示例
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# 安装规则
install(TARGETS sony2fuji_core
    ARCHIVE DESTINATION lib
)

if(BUILD_SHARED_LIB)
    install(TARGETS sony2fuji
        LIBRARY DESTINATION lib
        FRAMEWORK DESTINATION Library/Frameworks
    )
endif()

if(BUILD_CLI)
    install(TARGETS sony2fuji_cli
        RUNTIME DESTINATION bin
    )
endif()

install(DIRECTORY include/sony2fuji
    DESTINATION include
)
