#!/bin/sh
# movil_red.sh — el juego en red entre el PC y un aparato (etapa 10, pasos 7 y 8).
#
# El PC es el anfitrion (jugador 1, psx/build/ac3_libre con AC3_RED=anfitrion, el guion de los menus de
# tools/coop_vista.py) y el aparato el invitado (jugador 2, AC3 Dev con red=2 y su vista). Por el cable: adb reverse
# hace que el 127.0.0.1:7070 del aparato llegue al PC. Escribe depurar.txt (mision=N, coop=1, vista=2, red=2 y, con
# GUION=1, guion2=1: el guion del 2 de tools/coop_vista.py), abre la app, pulsa Jugar y lanza el anfitrion; al acabar
# deja el registro de los dos y borra depurar.txt. Debe dar 0 huellas distintas en los dos registros.
# Con VB=0 el anfitrion no acaba (para jugar: el dueno juega en el aparato; parar con Ctrl+C).
# (paso 8) MENU=1: por Wi-Fi y desde el menu, sin depurar.txt ni adb reverse: el PC crea la partida y el aparato entra
# en Cooperativo > Unirse y elige la primera de la lista (la del PC: su anuncio). Toques para 1920x1080 (Retroid).
#
# Uso: tools/movil_red.sh <serie> <dir_salida> [mision]   (p. ej. VB=8000 tools/movil_red.sh 2c85492e /tmp/red 1)
set -e
ADB=${ADB:-$HOME/tools/android-sdk/platform-tools/adb}
PKG=${PKG:-com.vzepec.ac3game.dev}
VB=${VB:-8000}
GUION=${GUION:-1}
DISC=${DISC:-"Rom/Ace Combat 3 - Electrosphere (USA)/Ace Combat 3 - Electrosphere (USA).bin"}
s=${1:?uso: tools/movil_red.sh <serie> <dir_salida> [mision]}
out=${2:?falta el directorio de salida}
mision=${3:-1}
F=/sdcard/Android/data/$PKG/files
cd "$(dirname "$0")/.."
mkdir -p "$out"
G0=$(python3 -c "import sys;sys.path.insert(0,'tools');import coop_vista;print(coop_vista.G0)")
extra=""
[ "$GUION" = 1 ] && extra="guion2=1\n"
frames=$VB
[ "$VB" = 0 ] && frames=216000
$ADB -s $s shell am force-stop $PKG
$ADB -s $s logcat -c
$ADB -s $s shell input keyevent KEYCODE_WAKEUP   # con la pantalla apagada la app no arranca
if [ "${MENU:-0}" = 1 ]; then
    $ADB -s $s reverse --remove tcp:7070 >/dev/null 2>&1 || true
    $ADB -s $s shell rm -f $F/depurar.txt
    env AC3_PAD_ID=73 AC3_MISSION=$mision AC3_COOP=1 AC3_PAD2_ID=73 AC3_WIDE=1 AC3_PACE=60 AC3_RED=anfitrion \
        AC3_RED_ESPERA=600 AC3_RED_NOMBRE=PC psx/build/ac3_libre "$DISC" - $frames "$out" 0 "$G0" > "$out/pc.log" 2>&1 &
    pc=$!
    $ADB -s $s shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
    sleep 4
    $ADB -s $s shell input tap 960 605    # Cooperativo
    sleep 2
    $ADB -s $s shell input tap 470 705    # Unirse
    sleep 4                               # (los anuncios llegan cada segundo)
    $ADB -s $s shell input tap 960 485    # la primera partida de la lista
    wait $pc || true
else
    $ADB -s $s reverse tcp:7070 tcp:7070 >/dev/null
    printf "mision=%d\ncoop=1\nvista=2\nred=2\n$extra" "$mision" | $ADB -s $s shell "cat > $F/depurar.txt"
    $ADB -s $s shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
    for i in 1 2 3 4; do   # Jugar: con el foco (Retroid) o pulsado (S24: 1170,520; Retroid: 960,536)
        sleep 3
        [ -n "$($ADB -s $s shell pidof $PKG:game)" ] && break
        if [ $i -eq 1 ]; then $ADB -s $s shell input keyevent KEYCODE_DPAD_CENTER
        elif [ $i -eq 2 ]; then $ADB -s $s shell input tap 1170 520
        else $ADB -s $s shell input tap 960 536; fi
    done
    env AC3_PAD_ID=73 AC3_MISSION=$mision AC3_COOP=1 AC3_PAD2_ID=73 AC3_WIDE=1 AC3_PACE=60 AC3_RED=anfitrion \
        AC3_RED_ESPERA=600 psx/build/ac3_libre "$DISC" - $frames "$out" 0 "$G0" > "$out/pc.log" 2>&1 || true
fi
sleep 2
$ADB -s $s logcat -d -s AC3Game | grep -a "red:\|coop:\|desarrollo:" > "$out/aparato.log" || true
$ADB -s $s shell am force-stop $PKG
$ADB -s $s shell rm -f $F/depurar.txt
echo "PC:      $(grep -a 'huellas comparadas' "$out/pc.log" | tail -1)"
echo "aparato: $(grep -a 'huellas comparadas' "$out/aparato.log" | tail -1 | sed 's/.*red: //')"
