#!/bin/sh
# retroid_mision.sh — prueba en la Retroid con el APK de desarrollo (etapa 8, pasos 9 y 10).
#
# Abre AC3 Dev (DESARROLLO=1 sh android_game/build.sh; el disco ya elegido una vez), elige la mision N
# en la lista (cruceta y A por adb), escribe <depurar> en depurar.txt (p. ej. "camara=70 conox=130"),
# captura la pantalla cada ~0,9 s durante <segundos> y al final:
#   - cuenta pixeles negros puros (< 8) en las franjas de 60 px de cada borde, a media altura (el
#     terreno que faltaba en 16:9, paso 10; el borde derecho sin la zona del boton circulo del mando
#     tactil, que asoma con el toque que abre el menu);
#   - resume el registro (fps, CPU del hilo del juego, llamadas de dibujo y tiempo de GPU por segundo).
# Claves utiles de depurar.txt: camara=N / palanca=N / alabeo=N (sticks fijos en N/100), conox=N (cono
# del terreno N/100 en vez de 1,3), cono=0, cielo=0, modo=0-3, escala=N, decimales=0|1.
#
# Uso: sh tools/retroid_mision.sh <salida> <mision> <segundos> ["<depurar>"]
#   p. ej. sh tools/retroid_mision.sh data/android/prueba18 18 12 "camara=70"
set -e
OUT=$1; M=$2; T=$3; D=$4
[ -n "$T" ] || { sed -n '2,15p' "$0"; exit 2; }
A=${ADB:-$HOME/tools/android-sdk/platform-tools/adb}
F=/sdcard/Android/data/com.vzepec.ac3game.dev/files/depurar.txt
mkdir -p "$OUT"; rm -f "$OUT"/c*.png "$OUT"/log.txt
$A shell am force-stop com.vzepec.ac3game.dev; $A logcat -c
if [ -n "$D" ]; then $A shell "echo '$D' > $F"; else $A shell rm -f $F; fi
$A shell monkey -p com.vzepec.ac3game.dev -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1; sleep 3
$A shell input tap 960 538; sleep 4                 # "Jugar": abre el disco y la lista
$A shell input keyevent KEYCODE_DPAD_DOWN
for i in $(seq 1 45); do $A shell input keyevent KEYCODE_DPAD_UP; done   # arriba del todo
for i in $(seq 1 "$M"); do $A shell input keyevent KEYCODE_DPAD_DOWN; done
$A shell input keyevent KEYCODE_BUTTON_A
for i in $(seq 1 40); do $A logcat -d -s AC3Game:* | grep -q "desarrollo: el juego arranca" && break; sleep 1; done
sleep 3
n=$((T * 2)); k=0
while [ $k -lt $n ]; do k=$((k + 1)); $A exec-out screencap -p > "$OUT/c$k.png"; sleep 0.4; done
$A logcat -d -s AC3Game:* > "$OUT/log.txt"
python3 - "$OUT" <<'PY'
import glob, re, sys
from PIL import Image
out = sys.argv[1]
tl = tr = 0
bad = []
for f in sorted(glob.glob(out + '/c*.png'), key=lambda f: int(re.findall(r'c(\d+)\.png$', f)[0])):
    a = Image.open(f).convert('L'); w, h = a.size
    l = sum(1 for x in range(0, 60) for y in range(h // 3, 3 * h // 4, 2) if a.getpixel((x, y)) < 8)
    r = sum(1 for x in range(w - 60, w) for y in range(h // 3, 700, 2) if a.getpixel((x, y)) < 8)
    tl += l; tr += r
    if l + r > 20: bad.append(f.rsplit('/', 1)[1])
print('bordes: negros izq %d, der %d; capturas con negro: %s' % (tl, tr, ' '.join(bad) or 'ninguna'))
L = open(out + '/log.txt', errors='replace').read().split('\n')
for i, s in enumerate(L):
    m = re.search(r'(\d\d:\d\d:\d\d).*fps (\d+), velocidad (\d+)%.*hilo del juego (\d+)%', s)
    if not m: continue
    g = next((x for x in L[i + 1:i + 4] if 'GPU x' in x), '')
    gm = re.search(r'(\d+) llamadas', g)
    t = next((x for x in L[i + 1:i + 5] if 'temporizador' in x), '')
    tm = re.search(r'([\d.]+) ms por vuelta de media', t)
    print('%s fps %s cpu %s%% llamadas %s gpu %s ms' % (m.group(1), m.group(2), m.group(4), gm.group(1) if gm else '-', tm.group(1) if tm else '-'))
PY
