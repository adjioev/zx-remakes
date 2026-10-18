# The game as Android's native library (libzxremakes.so), loaded by NativeActivity (build.sh)
cmake_minimum_required(VERSION 3.20)
project(zx_remakes_android C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../remaster/src)
add_library(zxremakes SHARED
  ${SRC}/main.cpp ${SRC}/runner.cpp ${SRC}/games.cpp ${SRC}/ballbreaker_game.cpp ${SRC}/bb_engine.cpp
  ${SRC}/bb_studio.cpp ${SRC}/jpeg.c ${SRC}/ballbreaker_hd.cpp ${SRC}/spectrum.cpp ${SRC}/zxcore.c
  ${SRC}/audio.cpp ${SRC}/music_remix.cpp ${SRC}/sfx.cpp ${SRC}/gamepad.cpp ${SRC}/gamefile.cpp ${SRC}/net.cpp
  ${SRC}/android.cpp)
target_include_directories(zxremakes PRIVATE ${RAYLIB_DIR}/include ${ANDROID_NDK}/sources/android/native_app_glue)
target_compile_definitions(zxremakes PRIVATE APP_BUNDLE=1 APP_VERSION="${APP_VERSION}" PROJECT_ROOT=".")
set_source_files_properties(${SRC}/zxcore.c ${SRC}/jpeg.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(zxremakes PRIVATE ${RAYLIB_DIR}/lib/libraylib.a log android EGL GLESv3 OpenSLES m)
# raylib reads the APK's files through a wrapped fopen; NativeActivity finds its entry point
target_link_options(zxremakes PRIVATE -Wl,--wrap=fopen -u ANativeActivity_onCreate -Wl,-z,max-page-size=16384)
