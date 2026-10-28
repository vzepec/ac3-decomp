#!/bin/bash
# El cooperativo en dos aparatos (etapa 9, paso 6): los dos simulan la misma partida con los mismos mandos (el del
# jugador 1, el guion de tools/misiones.py: guion=1; el del 2, el GUION2 de tools/coop_vista.py: guion2=1), pero el
# primero dibuja desde el jugador 1 y el segundo desde el 2 (vista=2). La huella de la RAM de cada VBlank debe ser la
# misma (el lockstep: cada aparato dibuja a su jugador). Con el APK de desarrollo instalado en los dos y por adb:
#
#   tools/movil_coop.sh <serie_A> <serie_B> <dir_salida> [mision]
#
# Escribe depurar.txt (huella=1, mision=N, guion=1, coop=1, guion2=1 y, en B, vista=2), abre la app, pulsa Jugar,
# espera a que haya VB VBlank anotados (por defecto 4000), se trae huella.bin y el registro (coste del redibujo del
# jugador 2), compara las dos huellas (tools/determinismo.py --comparar) y deja los aparatos como estaban.
set -e
ADB=${ADB:-$HOME/tools/android-sdk/platform-tools/adb}
PKG=${PKG:-com.vzepec.ac3game.dev}
VB=${VB:-4000}
a=${1:?uso: tools/movil_coop.sh <serie_A> <serie_B> <dir_salida> [mision]}
b=${2:?falta la serie del aparato B}
out=${3:?falta el directorio de salida}
mision=${4:-0}
F=/sdcard/Android/data/$PKG/files
need=$((VB * 35 * 8))
mkdir -p "$out"
run() {   # serie nombre claves_extra
    local s=$1 name=$2 extra=$3
    $ADB -s $s shell am force-stop $PKG
    $ADB -s $s shell rm -f $F/huella.bin
    $ADB -s $s logcat -c
    printf 'huella=1\nmision=%d\nguion=1\ncoop=1\nguion2=1\n%b' "$mision" "$extra" | $ADB -s $s shell "cat > $F/depurar.txt"
    $ADB -s $s shell input keyevent KEYCODE_WAKEUP   # con la pantalla apagada la app no arranca
    $ADB -s $s shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
    local i
    for i in 1 2 3 4; do   # Jugar: con el foco (Retroid) o pulsado (S24: 1170,520; Retroid: 960,536)
        sleep 3
        [ -n "$($ADB -s $s shell pidof $PKG:game)" ] && break
        if [ $i -eq 1 ]; then $ADB -s $s shell input keyevent KEYCODE_DPAD_CENTER
        elif [ $i -eq 2 ]; then $ADB -s $s shell input tap 1170 520
        else $ADB -s $s shell input tap 960 536; fi
    done
    local t=0 sz
    while :; do
        sz=$($ADB -s $s shell stat -c %s $F/huella.bin 2>/dev/null | tr -d '\r' || true)
        [ -n "$sz" ] && [ "$sz" -ge "$need" ] && break
        t=$((t + 2))
        if [ $t -gt 300 ]; then echo "$name: sin huella completa (${sz:-0} bytes)"; break; fi
        sleep 2
    done
    $ADB -s $s pull $F/huella.bin "$out/$name.hash" >/dev/null
    $ADB -s $s logcat -d -s AC3Game | grep -a "coop" > "$out/$name.log" || true
    $ADB -s $s shell am force-stop $PKG
    $ADB -s $s shell rm -f $F/depurar.txt $F/huella.bin
    echo "$name ($s): $(($(stat -c %s "$out/$name.hash") / 280)) VBlank"
}
run $a vista1 '' &
run $b vista2 'vista=2\n'
wait
grep -h "imagenes desde" "$out/vista2.log" | tail -1 | sed 's/.*coop: /aparato B: /'
python3 "$(dirname "$0")/determinismo.py" --comparar "$out/vista1.hash" "$out/vista2.hash"
