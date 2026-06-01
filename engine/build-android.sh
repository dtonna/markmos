# วิธีที่ 1: ใช้ toolchain file (NDK < 25)
# cmake -G "Android Gradle" -DANDROID_SDK=<path> -DANDROID_NDK=<path> -DANDROID_ABI=arm64-v8a -DANDROID_API=30 -S . -B build-android

# วิธี 2: ใช้ CMake รุ่นใหม่ (NDK >= 25) กับ toolchain file
cmake -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_API=30 -S . -B build-android

