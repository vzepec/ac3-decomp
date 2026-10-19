#!/bin/sh
# APK de AC3 Recomp. Por defecto (REGION=libre) el APK PUBLICABLE, el unico que se genera
# normalmente: solo codigo de este repositorio (interprete + capa psx/); el juego (ACE.BIN) se lee
# del disco del usuario. Salida: data/android/AC3Recomp-<version>.apk (paquete com.vzepec.ac3game).
# Antes de publicarlo: tools/apk_audit.py debe dar 0 coincidencias (docs/06-legal.md).
# Variantes PERSONALES (etapa 6; llevan codigo del juego traducido y ACE.BIN: NO se distribuyen):
#   all: APK universal con las dos versiones; la app elige la del disco por su
#        ejecutable de arranque. Requisitos: make native y make native-us.
#        Salida: data/android/ac3game-<version>.apk (paquete com.vzepec.ac3game)
#   eu / us: una sola version, ac3game-<version>-eu.apk / -us.apk, con paquete propio
#        (com.vzepec.ac3game.eu / .us) para que convivan en el aparato.
#   libre (por defecto, etapa 7): sin codigo traducido ni ACE.BIN; ejecuta el juego con el
#        interprete (psx/recomp/mipsi.c). No necesita data/ salvo la clave de firma
#        (data/android/debug.keystore: guardar copia, las actualizaciones deben ir firmadas igual).
# Los simbolos del codigo traducido americano llevan el sufijo _us (llvm-objcopy) para no chocar
# con los europeos (las funciones se llaman por su direccion: f_80025458...).
set -e
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
VERSION=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' AndroidManifest.xml)
OUT=$ROOT/data/android
REGION=${REGION:-libre}
case "$REGION" in
    all) REGIONS="eu us"; APK="ac3game-$VERSION-personal.apk"; PKG=com.vzepec.ac3game.personal; LABEL="AC3 Recomp (personal)" ;;
    eu) REGIONS=eu; APK="ac3game-$VERSION-eu.apk"; PKG=com.vzepec.ac3game.eu; LABEL="AC3 Recomp (EU)" ;;
    us) REGIONS=us; APK="ac3game-$VERSION-us.apk"; PKG=com.vzepec.ac3game.us; LABEL="AC3 Recomp (US)" ;;
    libre) REGIONS=; APK="AC3Recomp-$VERSION.apk"; PKG=; LABEL= ;;
    *) echo "REGION debe ser libre (por defecto), all, eu o us"; exit 1 ;;
esac
SDK=${ANDROID_HOME:-$HOME/tools/android-sdk}
NDK=${NDK:-$SDK/ndk/30.0.16248370}
BT=${BT:-$SDK/build-tools/37.0.0}
PLAT=${PLAT:-$SDK/platforms/android-36/android.jar}
JDK=${JDK:-$HOME/tools/jdk-21.0.12.1+1}
CC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang
GLUE=$NDK/sources/android/native_app_glue
KS=${KS:-$ROOT/data/android/debug.keystore}
B=$OUT/game_build_$REGION

TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
rdir() { [ "$1" = us ] && echo recomp_us || echo recomp; }
for r in $REGIONS; do
    for f in ace_$r overlay1 overlay2 overlay3 overlay4 overlay5 hle_gen; do
        [ -f "$ROOT/data/$(rdir $r)/$f.c" ] || { echo "falta data/$(rdir $r)/$f.c: make native (US: make us-prep ghidra-us native-us)"; exit 1; }
    done
    [ -f "$ROOT/data/exe/ace_$r.bin" ] || { echo "falta data/exe/ace_$r.bin"; exit 1; }
done
[ "${REGIONS#*us}" = "$REGIONS" ] || [ -f "$ROOT/data/recomp_us/ac3_region.h" ] || { echo "falta data/recomp_us/ac3_region.h: make us-prep"; exit 1; }

rm -rf "$B" && mkdir -p "$B/apk/lib/arm64-v8a" "$B/obj"
CF="-fPIC -w -I$ROOT/psx/include -I$ROOT/psx/recomp"
# ACE.BIN incrustado: ace_image (EU) y ace_image_us
: > "$B/ace_image.S"
for r in $REGIONS; do
    sfx=; [ "$r" = us ] && sfx=_us
    cat >> "$B/ace_image.S" <<ASM
    .section .rodata
    .global ace_image$sfx
    .global ace_image_len$sfx
    .balign 16
ace_image$sfx:
    .incbin "$ROOT/data/exe/ace_$r.bin"
ace_image_end$sfx:
    .balign 4
ace_image_len$sfx:
    .word ace_image_end$sfx - ace_image$sfx
ASM
done
for r in $REGIONS; do
    O="$B/obj_$r"; mkdir -p "$O"
    # el codigo traducido (grande): en paralelo, -O1 como en make native
    for f in ace_$r overlay1 overlay2 overlay3 overlay4 overlay5 hle_gen; do
        $CC -O1 $CF -c "$ROOT/data/$(rdir $r)/$f.c" -o "$O/$f.o" &
    done
    wait
    if [ "$r" = us ]; then
        "$TC/llvm-nm" --defined-only -g "$O"/*.o | awk 'NF == 3 {print $3, $3 "_us"}' | sort -u > "$B/us.syms"
        for o in "$O"/*.o; do "$TC/llvm-objcopy" --redefine-syms="$B/us.syms" "$o"; done
        $CC -O2 $CF -DREGION_US -include "$ROOT/data/recomp_us/ac3_region.h" -c src/regions.c -o "$B/obj/regions_us.o"
    else
        $CC -O2 $CF -c src/regions.c -o "$B/obj/regions_eu.o"
    fi
done
for f in "$ROOT"/psx/src/*.c "$ROOT/psx/recomp/recomp_rt.c" "$ROOT/psx/gl/psx_present.c"; do
    $CC -O2 $CF -c "$f" -o "$B/obj/$(basename "$f" .c).o"
done
if [ "$REGION" = libre ]; then
    for f in "$ROOT/psx/recomp/mipsi.c" "$ROOT/psx/recomp/ac3_versions.c" "$ROOT/psx/recomp/ac3_rerender.c" "$ROOT/psx/recomp/hle_eu.c" "$ROOT/psx/recomp/hle_us.c"; do
        $CC -O2 $CF -c "$f" -o "$B/obj/$(basename "$f" .c).o"
    done
    $CC -O2 $CF -DAC3_LIBRE -I"$GLUE" -c src/game_main.c -o "$B/obj/game_main.o"
else
    $CC -O2 $CF -I"$GLUE" -c src/game_main.c -o "$B/obj/game_main.o"
fi
$CC -O2 -fPIC -I"$GLUE" -w -c "$GLUE/android_native_app_glue.c" -o "$B/obj/glue.o"
[ -s "$B/ace_image.S" ] && $CC -c "$B/ace_image.S" -o "$B/obj/ace_image.o"
# 16 KB: segmentos alineados a 16384 (exigido por Android reciente)
$CC -shared "$B"/obj/*.o $(ls "$B"/obj_*/*.o 2>/dev/null) -landroid -llog -lEGL -lGLESv3 -laaudio -lm \
    -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384 \
    -u ANativeActivity_onCreate -o "$B/apk/lib/arm64-v8a/libac3game.so"

rm -rf "$B/classes" "$B/dex" && mkdir -p "$B/classes" "$B/dex"
"$JDK/bin/javac" --release 11 -nowarn -classpath "$PLAT" -d "$B/classes" java/com/vzepec/ac3game/*.java
JAVA_HOME="$JDK" PATH="$JDK/bin:$PATH" "$BT/d8" --min-api 26 --lib "$PLAT" --output "$B/dex" "$B"/classes/com/vzepec/ac3game/*.class
cp "$B/dex/classes.dex" "$B/apk/"

if [ -n "$PKG" ]; then
    # otro paquete y otro nombre visible; las clases siguen en com.vzepec.ac3game (JNI)
    sed "s/android:label=\"AC3 Recomp\"/android:label=\"$LABEL\"/" AndroidManifest.xml > "$B/AndroidManifest.xml"
    "$BT/aapt2" link -o "$B/base.apk" --manifest "$B/AndroidManifest.xml" -I "$PLAT" \
        --min-sdk-version 26 --target-sdk-version 36 --rename-manifest-package "$PKG"
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
if [ "$REGION" = libre ]; then
    echo "APK LIBRE (sin codigo ni datos del juego; comprobar con tools/apk_audit.py): $OUT/$APK ($(du -h "$OUT/$APK" | cut -f1))"
else
    echo "APK PERSONAL (no distribuir): $OUT/$APK ($(du -h "$OUT/$APK" | cut -f1))"
fi
