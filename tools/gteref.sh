#!/bin/sh
# gteref.sh — prepara la GTE de referencia (Mednafen, de Beetle PSX) y el binario de pruebas
# psx/build/ac3_gteref (etapa 6, paso 18).
#
# La GTE de Mednafen es GPL: se descarga a data/ref/ (fuera de git, como los datos del juego) en un
# commit fijo y se compila aparte con cabeceras minimas (PGXP y la pantalla ancha de libretro,
# apagados). Solo la enlaza el binario de pruebas; nunca entra en el repositorio ni en el APK.
# Uso: sh tools/gteref.sh   (luego: AC3_GTEREF=1 psx/build/ac3_gteref <disco> ... ; ver psx/test/gteref.c)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF="$ROOT/data/ref"
COMMIT=df2b53b306ba52d2116f8e8cc93b9ee33d239be7
mkdir -p "$REF"
if [ ! -f "$REF/bpsx/mednafen/psx/gte.c" ]; then
    rm -rf "$REF/bpsx"
    git clone -q --filter=blob:none --no-checkout https://github.com/libretro/beetle-psx-libretro "$REF/bpsx"
    git -C "$REF/bpsx" sparse-checkout set --no-cone 'mednafen/psx/gte*'
    git -C "$REF/bpsx" checkout -q "$COMMIT"
fi
SH="$REF/shim"
rm -rf "$SH"
mkdir -p "$SH/inc/compat" "$SH/inc/math" "$SH/mednafen/psx" "$SH/mednafen/pgxp"
# gte.c sin GTE_StateAction (guardado de estado, depende de todo Mednafen)
awk '/^int GTE_StateAction/ {skip = 1} skip && /^}/ {skip = 0; next} !skip' "$REF/bpsx/mednafen/psx/gte.c" > "$SH/mednafen/psx/gte.c"
cp "$REF/bpsx/mednafen/psx/gte.h" "$SH/mednafen/psx/gte.h"
echo '#include <stdbool.h>' > "$SH/inc/boolean.h"
: > "$SH/inc/retro_miscellaneous.h"
: > "$SH/inc/compat/intrinsics.h"
cat > "$SH/inc/math/float_minmax.h" <<'X'
static inline float float_min(float a, float b) { return a < b ? a : b; }
static inline float float_max(float a, float b) { return a > b ? a : b; }
X
cat > "$SH/mednafen/mednafen-types.h" <<'X'
#include <stdint.h>
#include <stdbool.h>
#define INLINE inline
#define MDFN_NOWARN_UNUSED __attribute__((unused))
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
X
echo 'static inline uint32_t MDFN_lzcount32(uint32_t v) { return v ? (uint32_t)__builtin_clz(v) : 32; }' > "$SH/mednafen/math_ops.h"
: > "$SH/mednafen/state_helpers.h"
echo 'typedef struct StateMem StateMem;' > "$SH/mednafen/state.h"
cat > "$SH/mednafen/pgxp/pgxp_gte.h" <<'X'
#define PGXP_NCLIP_IMPL 1
#define PGXP_LIT 0
static inline void PGXP_pushSXYZ2f(float x, float y, float z, uint32_t v) { (void)x; (void)y; (void)z; (void)v; }
static inline void PGXP_pushRGBf(float r, float g, float b, ...) { (void)r; (void)g; (void)b; }
static inline int PGXP_NCLIP_valid(uint32_t a, uint32_t b, uint32_t c) { (void)a; (void)b; (void)c; return 0; }
static inline int PGXP_NCLIP_sign(void) { return 0; }
static inline int32_t PGXP_NCLIP_preserve_magnitude(int32_t s, int sg) { (void)sg; return s; }
#define PGXP_GTE_SetFogContext(...) ((void)0)
#define PGXP_DiagDump(...) ((void)0)
#define PGXP_pushSXYZ(...) ((void)0)
#define PGXP_CacheVertex(...) ((void)0)
#define PGXP_GetVertex(...) 0
#define PGXP_InvalidateVertexFIFO(...) ((void)0)
X
echo 'static inline unsigned PGXP_GetModes(void) { return 0; }' > "$SH/mednafen/pgxp/pgxp_main.h"
printf 'static const bool widescreen_hack = false;\nstatic const int widescreen_hack_aspect_ratio_setting = 1;\n' > "$SH/beetle_psx_globals.h"
cat > "$SH/glue.c" <<'X'
#include <stdbool.h>
#include <stdint.h>
bool psx_gte_overclock = false;
unsigned compat_clz_u16(uint16_t val) { return (unsigned)__builtin_clz((uint32_t)val << 16 | 0x8000); }
X
cc -O2 -w -c -I "$SH/inc" -I "$SH/mednafen" "$SH/mednafen/psx/gte.c" -o "$SH/gte_ref.o"
cc -O2 -c "$SH/glue.c" -o "$SH/glue.o"
cd "$ROOT"
mkdir -p psx/build
cc -O2 -w -D_GNU_SOURCE -DAC3_LIBRE -DAC3_GTEREF -Ipsx/include -Ipsx/recomp -Iandroid/build/inc psx/recomp/native_main.c \
    psx/recomp/mipsi.c psx/recomp/recomp_rt.c psx/recomp/ac3_versions.c psx/recomp/hle_eu.c psx/recomp/hle_us.c psx/src/*.c \
    psx/gl/psx_hw.c psx/gl/psx_hwcmp.c psx/test/gteref.c "$SH/gte_ref.o" "$SH/glue.o" \
    /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2 -lm -lpthread -o psx/build/ac3_gteref
echo "listo: psx/build/ac3_gteref (GTE de referencia: Beetle PSX $COMMIT)"
