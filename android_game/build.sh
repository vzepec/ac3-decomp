#!/bin/sh
# APK del JUEGO RECOMPILADO (etapa 6, paso 4). COMPILACION PERSONAL: enlaza el codigo del
# juego traducido a C (data/recomp/*.c) e incrusta ACE.BIN (data/exe/ace_eu.bin), ambos
# generados desde el disco del usuario. El APK resultante queda en data/android/ (fuera de
# git) y NO SE DISTRIBUYE (docs/06-legal.md). Requisitos: make native (genera data/recomp/).
# Salida: data/android/ac3game-<version>.apk
# REGION=us (por defecto eu): la version americana, desde data/recomp_us/ (make native-us) y
# data/exe/ace_us.bin; sale data/android/ac3game-<version>-us.apk, con otro paquete
# (com.vzepec.ac3game.us) para que conviva con la europea en el aparato.
set -e
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
VERSION=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' AndroidManifest.xml)
OUT=$ROOT/data/android
REGION=${REGION:-eu}
case "$REGION" in
    eu) APK="ac3game-$VERSION.apk"; RDIR=recomp; STEM=ace_eu; BDIR=game_build; RINC= ;;
    us) APK="ac3game-$VERSION-us.apk"; RDIR=recomp_us; STEM=ace_us; BDIR=game_build_us
        RINC="-include $ROOT/data/recomp_us/ac3_region.h"
        [ -f "$ROOT/data/recomp_us/ac3_region.h" ] || { echo "falta data/recomp_us/ac3_region.h: make us-prep"; exit 1; } ;;
    *) echo "REGION debe ser eu o us"; exit 1 ;;
esac
SDK=${ANDROID_HOME:-$HOME/tools/android-sdk}
NDK=${NDK:-$SDK/ndk/30.0.16248370}
BT=${BT:-$SDK/build-tools/37.0.0}
PLAT=${PLAT:-$SDK/platforms/android-36/android.jar}
JDK=${JDK:-$HOME/tools/jdk-21.0.12.1+1}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang
GLUE=$NDK/sources/android/native_app_glue
KS=${KS:-$ROOT/data/android/debug.keystore}
B=$OUT/$BDIR

for f in $STEM overlay1 overlay2 overlay3 overlay4 overlay5 hle_gen; do
    [ -f "$ROOT/data/$RDIR/$f.c" ] || { echo "falta data/$RDIR/$f.c: ejecuta antes make native (US: make native-us)"; exit 1; }
done
[ -f "$ROOT/data/exe/$STEM.bin" ] || { echo "falta data/exe/$STEM.bin"; exit 1; }

rm -rf "$B/apk" && mkdir -p "$B/apk/lib/arm64-v8a" "$B/obj"
# ACE.BIN incrustado
cat > "$B/ace_image.S" <<ASM
    .section .rodata
    .global ace_image
    .global ace_image_len
    .balign 16
ace_image:
    .incbin "$ROOT/data/exe/$STEM.bin"
ace_image_end:
    .balign 4
ace_image_len:
    .word ace_image_end - ace_image
ASM
CF="-fPIC -w -I$ROOT/psx/include -I$ROOT/psx/recomp"
# el codigo traducido (grande): en paralelo, -O1 como en make native
for f in $STEM overlay1 overlay2 overlay3 overlay4 overlay5 hle_gen; do
    $CC -O1 $CF -c "$ROOT/data/$RDIR/$f.c" -o "$B/obj/$f.o" &
done
wait
for f in "$ROOT"/psx/src/*.c "$ROOT/psx/recomp/recomp_rt.c" "$ROOT/psx/gl/psx_present.c"; do
    $CC -O2 $CF -c "$f" -o "$B/obj/$(basename "$f" .c).o"
done
$CC -O2 $CF $RINC -I"$GLUE" -c src/game_main.c -o "$B/obj/game_main.o"
$CC -O2 -fPIC -I"$GLUE" -w -c "$GLUE/android_native_app_glue.c" -o "$B/obj/glue.o"
$CC -c "$B/ace_image.S" -o "$B/obj/ace_image.o"
# 16 KB: segmentos alineados a 16384 (exigido por Android reciente)
$CC -shared "$B"/obj/*.o -landroid -llog -lEGL -lGLESv3 -laaudio -lm \
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 \
    -u ANativeActivity_onCreate -o "$B/apk/lib/arm64-v8a/libac3game.so"

rm -rf "$B/classes" "$B/dex" && mkdir -p "$B/classes" "$B/dex"
"$JDK/bin/javac" --release 11 -nowarn -classpath "$PLAT" -d "$B/classes" java/com/vzepec/ac3game/*.java
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$BT/d8" --min-api 26 --lib "$PLAT" --output "$B/dex" "$B"/classes/com/vzepec/ac3game/*.class
cp "$B/dex/classes.dex" "$B/apk/"

if [ "$REGION" = us ]; then
    # otro paquete y otro nombre visible; las clases siguen en com.vzepec.ac3game (JNI)
    sed 's/android:label="AC3 Recomp"/android:label="AC3 Recomp (US)"/' AndroidManifest.xml > "$B/AndroidManifest.xml"
    "$BT/aapt2" link -o "$B/base.apk" --manifest "$B/AndroidManifest.xml" -I "$PLAT" \
        --min-sdk-version 26 --target-sdk-version 36 --rename-manifest-package com.vzepec.ac3game.us
else
"$BT/aapt2" link -o "$B/base.apk" --manifest AndroidManifest.xml -I "$PLAT" \
    --min-sdk-version 26 --target-sdk-version 36
fi
(cd "$B/apk" && zip -q -r ../base.apk lib classes.dex)
"$BT/zipalign" -P 16 -f 4 "$B/base.apk" "$B/aligned.apk"
if [ ! -f "$KS" ]; then
    mkdir -p "$(dirname "$KS")"
    "$JDK/bin/keytool" -genkeypair -keystore "$KS" -storepass android -keypass android \
        -alias debug -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=AC3 debug" >/dev/null 2>&1
fi
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$BT/apksigner" sign --ks "$KS" --ks-pass pass:android \
    --key-pass pass:android --out "$OUT/$APK" "$B/aligned.apk"
"$BT/zipalign" -c -P 16 4 "$OUT/$APK"
echo "APK PERSONAL (no distribuir): $OUT/$APK ($(du -h "$OUT/$APK" | cut -f1))"
