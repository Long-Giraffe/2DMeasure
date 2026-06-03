cmake_minimum_required(VERSION 3.16)
if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()

project(MeasureRecipeApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(QT5_ROOT "C:/Qt/Qt5.12.12/5.12.12/msvc2017_64" CACHE PATH "Qt 5.12 msvc2017_64 root")
set(OPENCV_ROOT "E:/Code/3rdParty/opencv" CACHE PATH "OpenCV root with include/lib/bin folders")
list(APPEND CMAKE_PREFIX_PATH "${QT5_ROOT}")

find_package(Qt5 REQUIRED COMPONENTS Widgets)

set(OPENCV_INCLUDE_DIR "${OPENCV_ROOT}/include")
set(OPENCV_LIB_DIR "${OPENCV_ROOT}/lib")
set(OPENCV_BIN_DIR "${OPENCV_ROOT}/bin")

set(OPENCV_RELEASE_LIBS
    "${OPENCV_LIB_DIR}/opencv_core4110.lib"
    "${OPENCV_LIB_DIR}/opencv_imgproc4110.lib"
    "${OPENCV_LIB_DIR}/opencv_imgcodecs4110.lib"
)
set(OPENCV_DEBUG_LIBS
    "${OPENCV_LIB_DIR}/opencv_core4110d.lib"
    "${OPENCV_LIB_DIR}/opencv_imgproc4110d.lib"
    "${OPENCV_LIB_DIR}/opencv_imgcodecs4110d.lib"
)

add_executable(measure_app
    src/app/main.cpp
    src/core/CaliperDetector.cpp
    src/core/Recipe.cpp
    src/ui/CurveWidget.cpp
    src/ui/ImageView.cpp
    src/ui/MainWindow.cpp
)
target_include_directories(measure_app PRIVATE src)
target_include_directories(measure_app SYSTEM PRIVATE "${OPENCV_INCLUDE_DIR}")
target_link_libraries(measure_app PRIVATE
    Qt5::Widgets
    optimized ${OPENCV_RELEASE_LIBS}
    debug ${OPENCV_DEBUG_LIBS}
)

add_executable(caliper_validation caliper_validation.cpp)
target_compile_features(caliper_validation PRIVATE cxx_std_17)

add_executable(edge_bias_probe
    tools/edge_bias_probe.cpp
    src/core/CaliperDetector.cpp
)
target_include_directories(edge_bias_probe PRIVATE src)
target_include_directories(edge_bias_probe SYSTEM PRIVATE "${OPENCV_INCLUDE_DIR}")
target_link_libraries(edge_bias_probe PRIVATE
    optimized ${OPENCV_RELEASE_LIBS}
    debug ${OPENCV_DEBUG_LIBS}
)

if(MSVC)
    target_compile_options(measure_app PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(caliper_validation PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(edge_bias_probe PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(measure_app PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(caliper_validation PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(edge_bias_probe PRIVATE -Wall -Wextra -pedantic)
endif()

set(QT_BIN_DIR "${QT5_ROOT}/bin")
set(QT_PLUGIN_DIR "${QT5_ROOT}/plugins")

add_custom_command(TARGET measure_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_BIN_DIR}/Qt5Cored.dll,${QT_BIN_DIR}/Qt5Core.dll>"
        "$<IF:$<CONFIG:Debug>,${QT_BIN_DIR}/Qt5Guid.dll,${QT_BIN_DIR}/Qt5Gui.dll>"
        "$<IF:$<CONFIG:Debug>,${QT_BIN_DIR}/Qt5Widgetsd.dll,${QT_BIN_DIR}/Qt5Widgets.dll>"
        "$<IF:$<CONFIG:Debug>,${OPENCV_BIN_DIR}/opencv_core4110d.dll,${OPENCV_BIN_DIR}/opencv_core4110.dll>"
        "$<IF:$<CONFIG:Debug>,${OPENCV_BIN_DIR}/opencv_imgproc4110d.dll,${OPENCV_BIN_DIR}/opencv_imgproc4110.dll>"
        "$<IF:$<CONFIG:Debug>,${OPENCV_BIN_DIR}/opencv_imgcodecs4110d.dll,${OPENCV_BIN_DIR}/opencv_imgcodecs4110.dll>"
        "$<TARGET_FILE_DIR:measure_app>"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:measure_app>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${QT_PLUGIN_DIR}/platforms/qwindowsd.dll,${QT_PLUGIN_DIR}/platforms/qwindows.dll>"
        "$<TARGET_FILE_DIR:measure_app>/platforms"
)
