#!/bin/bash
# Graba 2 s de la pantalla del movil (adb screenrecord: en el S24, 120 imagenes por segundo) y hace una
# hoja con las 24 primeras imagenes seguidas, recortadas (etapa 8, paso 13: asi se vio el avion pequeno de
# la primera intermedia con el postquemador, una de cada cuatro imagenes).
#
#   tools/movil_grabar.sh <dir_salida> [x1,y1,x2,y2]
#
# Deja <dir_salida>.mp4, <dir_salida>/f001.png... y <dir_salida>_hoja.png. El recorte por defecto es
# el avion en la vista exterior a 2340x1080. Con depurar.txt (botones=2048 mantiene R1, pulsar=1
# pulsa SELECT una vez, alabeo=40 inclina la palanca) se repite la prueba sin tocar el movil.
set -e
ADB=${ADB:-$HOME/tools/android-sdk/platform-tools/adb}
out=${1:?uso: tools/movil_grabar.sh <dir_salida> [x1,y1,x2,y2]}
box=${2:-700,380,1640,1080}
$ADB shell screenrecord --time-limit 2 /sdcard/Download/ac3rec.mp4
$ADB pull /sdcard/Download/ac3rec.mp4 "$out.mp4" >/dev/null
$ADB shell rm /sdcard/Download/ac3rec.mp4
mkdir -p "$out"
rm -f "$out"/f*.png
ffmpeg -v quiet -i "$out.mp4" -vsync 0 -frames:v 24 "$out/f%03d.png"
python3 - "$out" "$box" <<'P'
import sys, glob
from PIL import Image, ImageDraw
d, box = sys.argv[1], tuple(int(v) for v in sys.argv[2].split(','))
fs = sorted(glob.glob(d + '/f*.png'))
W, H = (box[2] - box[0]) // 3, (box[3] - box[1]) // 3
sh = Image.new('RGB', (W * 6, H * 4))
dr = ImageDraw.Draw(sh)
for i, f in enumerate(fs):
    sh.paste(Image.open(f).convert('RGB').crop(box).resize((W, H)), ((i % 6) * W, (i // 6) * H))
    dr.text(((i % 6) * W + 3, (i // 6) * H + 3), str(i + 1), fill='yellow')
sh.save(d + '_hoja.png')
print(d + '_hoja.png')
P
