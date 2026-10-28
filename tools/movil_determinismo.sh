#!/bin/bash
# Determinismo en el movil (etapa 9, paso 1): la misma partida, varias veces y con opciones distintas,
# debe dar la misma RAM en cada VBlank. Con el APK de desarrollo instalado y el movil por adb:
#
#   tools/movil_determinismo.sh <dir_salida> [mision]
#
# Por configuracion: escribe depurar.txt (huella=1: la huella de la RAM en cada VBlank; guion=1: el
# mando del guion de tools/misiones.py; mision=N: sin la lista), abre la app, pulsa Jugar, espera a
# que haya VB VBlank anotados (por defecto 4000) y se trae huella.bin. Al final las compara con la
# primera (tools/determinismo.py --comparar) y deja el movil como estaba (sin depurar.txt ni huella.bin).
set -e
ADB=${ADB:-$HOME/tools/android-sdk/platform-tools/adb}
PKG=${PKG:-com.vzepec.ac3game.dev}
VB=${VB:-4000}
out=${1:?uso: tools/movil_determinismo.sh <dir_salida> [mision]}
mision=${2:-1}
F=/sdcard/Android/data/$PKG/files
# nombre:claves de depurar.txt separadas por espacios
# (control_mando: guion=2, un misil mas en el VBlank 3000; DEBE salir distinta)
CONFIGS=${CONFIGS:-"base: base_otra_vez: control_mando:guion=2 hz60:hz=60 sin_intermedias:intermedias=0 escala1:escala=1 opcion_43:modo=2 opcion_original:modo=3"}
mkdir -p "$out"
need=$((VB * 35 * 8))
for c in $CONFIGS; do
    name=${c%%:*}
    extra=${c#*:}
    $ADB shell am force-stop $PKG
    $ADB shell rm -f $F/huella.bin
    { printf 'huella=1\nmision=%d\n' "$mision"; case " ${extra//,/ } " in *" guion="*) ;; *) echo guion=1 ;; esac
      for kv in ${extra//,/ }; do echo "$kv"; done; } |
        $ADB shell "cat > $F/depurar.txt"
    $ADB shell input keyevent KEYCODE_WAKEUP   # (etapa 10) con la pantalla apagada la app no arranca
    $ADB shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
    for i in 1 2 3 4; do   # Jugar: con el foco (Retroid) o pulsado (S24: 1170,520; Retroid: 960,536)
        sleep 3
        [ -n "$($ADB shell pidof $PKG:game)" ] && break
        if [ $i -eq 1 ]; then $ADB shell input keyevent KEYCODE_DPAD_CENTER
        elif [ $i -eq 2 ]; then $ADB shell input tap 1170 520
        else $ADB shell input tap 960 536; fi
    done
    t=0
    while :; do
        sz=$($ADB shell stat -c %s $F/huella.bin 2>/dev/null | tr -d '\r' || true)
        [ -n "$sz" ] && [ "$sz" -ge "$need" ] && break
        t=$((t + 2))
        if [ $t -gt 240 ]; then echo "$name: sin huella completa (${sz:-0} bytes)"; break; fi
        sleep 2
    done
    $ADB pull $F/huella.bin "$out/$name.hash" >/dev/null
    echo "$name: $(($(stat -c %s "$out/$name.hash") / 280)) VBlank"
done
$ADB shell am force-stop $PKG
$ADB shell rm -f $F/depurar.txt $F/huella.bin
first=$(echo $CONFIGS | cut -d' ' -f1); first=${first%%:*}
rest=""
for c in $CONFIGS; do n=${c%%:*}; [ "$n" != "$first" ] && rest="$rest $out/$n.hash"; done
python3 "$(dirname "$0")/determinismo.py" --comparar "$out/$first.hash" $rest
