#!/bin/sh
# Compila y empaqueta el APK de la rebanada vertical sin Gradle:
# NDK (clang) + aapt2 + zipalign + apksigner. Salida: android/build/ac3slice-<version>.apk
# (la version sale de android:versionName del manifiesto).
# El APK no contiene datos del juego.
set -e
cd "$(dirname "$0")"
VERSION=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' AndroidManifest.xml)
APK="ac3slice-$VERSION.apk"
SDK=${ANDROID_HOME:-$HOME/tools/android-sdk}
NDK=${NDK:-$SDK/ndk/30.0.16248370}
BT=${BT:-$SDK/build-tools/37.0.0}
PLAT=${PLAT:-$SDK/platforms/android-36/android.jar}
JDK=${JDK:-$HOME/tools/jdk-21.0.12.1+1}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang
GLUE=$NDK/sources/android/native_app_glue
KS=${KS:-../data/android/debug.keystore}

rm -rf build/apk && mkdir -p build/apk/lib/arm64-v8a
# 16 KB: segmentos alineados a 16384 (exigido por Android reciente)
$CC -O2 -fPIC -shared -Wall -Wextra -Wno-unused-parameter \
    -I"$GLUE" -Isrc -I../psx/include src/main.c src/render.c src/ace.c src/scene.c src/sound.c src/audio.c src/music.c \
    ../psx/src/xa.c "$GLUE/android_native_app_glue.c" \
    -landroid -llog -lEGL -lGLESv3 -laaudio -lm \
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 \
    -u ANativeActivity_onCreate \
    -o build/apk/lib/arm64-v8a/libac3slice.so

# puente Java (selector de archivos): javac + d8 -> classes.dex
rm -rf build/classes && mkdir -p build/classes build/dex
"$JDK/bin/javac" --release 11 -nowarn -classpath "$PLAT" -d build/classes java/com/vzepec/ac3slice/*.java
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$BT/d8" --min-api 26 --lib "$PLAT" --output build/dex build/classes/com/vzepec/ac3slice/*.class
cp build/dex/classes.dex build/apk/

"$BT/aapt2" link -o build/base.apk --manifest AndroidManifest.xml -I "$PLAT" \
    --min-sdk-version 26 --target-sdk-version 36
(cd build/apk && zip -q -r ../base.apk lib classes.dex)
"$BT/zipalign" -P 16 -f 4 build/base.apk build/aligned.apk

if [ ! -f "$KS" ]; then
    mkdir -p "$(dirname "$KS")"
    "$JDK/bin/keytool" -genkeypair -keystore "$KS" -storepass android -keypass android \
        -alias debug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=AC3 Slice debug" >/dev/null 2>&1
fi
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$BT/apksigner" sign --ks "$KS" --ks-pass pass:android \
    --key-pass pass:android --out "build/$APK" build/aligned.apk
"$BT/zipalign" -c -P 16 4 "build/$APK"
echo "APK: $(pwd)/build/$APK ($(du -h "build/$APK" | cut -f1))"
