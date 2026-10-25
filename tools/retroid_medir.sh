#!/bin/sh
# retroid_medir.sh <salida> [segundos] [config]: mide el rendimiento en el aparato conectado por adb
# (etapa 8, paso 5). Arranca la app, entra en la primera mision sin tocar nada (START, X por los
# menus, START salta el prologo), fuerza la configuracion por el archivo de depuracion (por defecto
# "escala=4 filtro=1") y graba el registro de AC3Game mientras el avion vuela solo.
# Resumen: python3 tools/logcat_resumen.py <salida>
A=${ADB:-$HOME/tools/android-sdk/platform-tools/adb}
OUT=$1; SECS=${2:-90}; CFG=${3:-escala=4 filtro=1}
TMP=$(mktemp -d)
F=/sdcard/Android/data/com.vzepec.ac3game/files/depurar.txt
$A shell am force-stop com.vzepec.ac3game
$A shell monkey -p com.vzepec.ac3game -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 2
$A shell input tap 960 538          # Jugar (pantalla de 1920x1080)
sleep 10
$A shell input keyevent KEYCODE_BUTTON_START
dark=0
for i in $(seq 1 60); do
    sleep 3
    $A exec-out screencap -p > "$TMP/s.png"
    m=$(python3 -c "from PIL import Image, ImageStat; print(int(ImageStat.Stat(Image.open('$TMP/s.png').convert('L').resize((96,54))).mean[0]))")
    if [ "$m" -lt 25 ]; then dark=1; $A shell input keyevent KEYCODE_BUTTON_START   # el prologo: START lo salta
    elif [ "$dark" = 1 ]; then break                                                 # ya en la mision
    else $A shell input keyevent KEYCODE_BUTTON_A; fi
done
$A shell "echo '$CFG' > $F"
sleep 5
$A logcat -c
timeout "$SECS" $A logcat -s AC3Game > "$OUT"
$A shell rm -f $F
rm -rf "$TMP"
