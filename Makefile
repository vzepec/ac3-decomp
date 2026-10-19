# Atajos del proyecto. Requiere solo Python 3.
PY  ?= python3
GHIDRA ?= $(HOME)/tools/ghidra_12.1.4_PUBLIC
JDK    ?= $(HOME)/tools/jdk-21.0.12.1+1
HEADLESS = JAVA_HOME="$(JDK)" PATH="$(JDK)/bin:$$PATH" "$(GHIDRA)/support/analyzeHeadless"
EU  := Rom/Ace Combat 3 - Electrosphere (Europe) (EnFrDeEsIt)/Ace Combat 3 - Electrosphere (Europe) (En,Fr,De,Es,It).bin
US  := Rom/Ace Combat 3 - Electrosphere (USA)/Ace Combat 3 - Electrosphere (USA).bin
DISC ?= $(EU)

.PHONY: help brief files info index inventory records tags extract-bpb ulz-check sections textures models sounds acebin overlays apk ghidra-import ghidra-stats check psx-check psyq-api psx-scene progress psx-display psx-android recomp recomp-check recomp-record native us-prep ghidra-us recomp-us recomp-check-us native-us native-us-build

help:
	@echo "make brief      - estado del proyecto + comprobacion de entorno"
	@echo "make files      - tabla de archivos de ambos discos"
	@echo "make info       - cabeceras PS-EXE de los ejecutables"
	@echo "make index      - cabeceras de los indices de assets"
	@echo "make inventory  - inventario de los 199 recursos"
	@echo "make records    - cabecera/tabla de secciones de cada registro"
	@echo "make tags       - semantica del tag del indice (BPB: tamano en u32)"
	@echo "make extract-bpb - extrae las 127 entradas de ACE.BPB a data/bpb/"
	@echo "make ulz-check  - valida el descompresor ULZ en todas las secciones"
	@echo "make sections   - resumen de secciones (descomprimidas) por tipo"
	@echo "make models     - valida el formato de modelos (vertices) en todo el BPB"
	@echo "make sounds     - valida los bancos de sonido del SPU"
	@echo "make textures   - vuelca a data/png/ las TIM del registro REC (def. 0006)"
	@echo "make acebin     - descomprime ACE.BIN (EU y US) a data/exe/"
	@echo "make overlays   - lista y extrae los overlays de codigo a data/exe/ovl/"
	@echo "make apk        - compila la rebanada vertical de Android (android/build/ac3slice-<version>.apk)"
	@echo "make ghidra-import - importa y analiza data/exe/ace_eu.exe en Ghidra (~4 min)"
	@echo "make ghidra-stats  - cifras de funciones -> data/ghidra/stats_eu.txt"
	@echo "make check      - comprueba que las herramientas corren"
	@echo "make psx-check  - etapa 5: pruebas de la GTE y comparacion de psx/ con ACE.BIN (oraculo MIPS)"
	@echo "make psyq-api   - etapa 5: API PsyQ que usa el juego"
	@echo "make psx-scene  - etapa 5: terreno de la mision 6 dibujado con la capa PsyQ (GTE + GPU)"
	@echo "make progress   - etapa 6: recalcula docs/progreso.json y la infografia del README"
	@echo "make psx-display - etapa 6: salida de video (VRAM a RGBA y GLES) con pruebas"
	@echo "make psx-android - etapa 6: compila la capa para Android (arm64) con el NDK"
	@echo "make recomp     - etapa 6: recompila el juego a C (data/recomp, fuera de git)"
	@echo "make recomp-check - etapa 6: valida cada funcion recompilada contra el interprete"
	@echo "make recomp-record - etapa 6: valida con estados reales (arranque en el interprete)"
	@echo "make native     - etapa 6: el juego recompilado, nativo en el PC (capturas)"
	@echo "make us-prep    - version US: ACE.BIN, BPB, overlays y perfil de direcciones (data/recomp_us)"
	@echo "make ghidra-us  - version US: analisis y volcado de Ghidra (data/ghidra_us, ~10 min)"
	@echo "make recomp-us / recomp-check-us / native-us - recompilacion, validacion y arranque de la US"
	@echo ""
	@echo "Usa DISC=... para apuntar a otro disco (por defecto el europeo)."

files:
	@$(PY) tools/psxiso.py "$(EU)" list
	@echo ""
	@$(PY) tools/psxiso.py "$(US)" list

info:
	@$(PY) tools/psexe.py "$(DISC)"

index:
	@$(PY) tools/acepack.py "$(DISC)" index

inventory:
	@$(PY) tools/acepack.py "$(DISC)" inventory

records:
	@$(PY) tools/acepack.py "$(DISC)" records

tags:
	@$(PY) tools/acepack.py "$(DISC)" tags

REC ?= 0006

ulz-check:
	@$(PY) tools/ulz.py --check data/bpb

sections:
	@$(PY) tools/acerec.py summary data/bpb

models:
	@$(PY) tools/acemodel.py check data/bpb
	@$(PY) tools/acemodel.py normals data/bpb

sounds:
	@$(PY) tools/acesnd.py check data/bpb

textures:
	@mkdir -p data/sec data/png
	@$(PY) tools/acerec.py dump data/bpb/$(REC)_*.bin data/sec
	@for f in data/sec/$(REC)_*_s*.bin; do $(PY) tools/tim2png.py png "$$f" data/png; done

extract-bpb:
	@mkdir -p data/bpb
	@$(PY) tools/acepack.py "$(DISC)" extract BPB data/bpb

acebin:
	@mkdir -p data/exe
	@$(PY) tools/acebin.py "$(EU)" data/exe/ace_eu.exe --psexe
	@$(PY) tools/acebin.py "$(US)" data/exe/ace_us.exe --psexe

apk:
	@android/build.sh

overlays:
	@$(PY) tools/aceovl.py extract data/exe/ace_eu.bin data/bpb data/exe/ovl

ghidra-import:
	@mkdir -p data/ghidra
	@$(HEADLESS) data/ghidra ac3 -import data/exe/ace_eu.exe -overwrite -max-cpu 4 \
		-scriptPath tools/ghidra -postScript CreateMissingFunctions.java \
		-postScript FixFunctionStarts.java 0x8008BCBC 0x8008BE4C 0x8008B914 0x8008BA18 0x8008BB1C 0x8008B1A8 \
		-postScript ApplyNames.java tools/ghidra/names.tsv > data/ghidra/import_eu.log 2>&1
	@grep -E "funciones creadas|nombres aplicados" data/ghidra/import_eu.log | sed 's/.*> //' 
	@grep -E "REPORT" data/ghidra/import_eu.log | tail -3

ghidra-stats:
	@$(HEADLESS) data/ghidra ac3 -process ace_eu.exe -noanalysis -scriptPath tools/ghidra \
		-postScript AceStats.java data/ghidra/stats_eu > data/ghidra/stats_eu.log 2>&1
	@grep -vE "^bloque [A-Z_0-9.]+ " data/ghidra/stats_eu.txt
	@$(PY) tools/ghidra/libsplit.py data/ghidra/stats_eu_funcs.tsv "$(GHIDRA)/Ghidra/Extensions/ghidra_psx_ldr/data/psyq/450"

brief:
	@echo "=============================================================="
	@echo " Ace Combat 3: Electrosphere - ingenieria inversa y port"
	@echo "=============================================================="
	@echo ""
	@sed -n '1,/^## Sin empezar/p' STATUS.md | sed '/^## Sin empezar/d'
	@echo "--------------------------------------------------------------"
	@echo "ENTORNO"
	@$(PY) -c "import sys; print('  python', sys.version.split()[0])"
	@printf "  ghidra: "; test -x "$(GHIDRA)/support/analyzeHeadless" && echo "$(GHIDRA)" || echo "NO instalado (ver docs/09-codigo.md)"
	@printf "  discos en Rom/: "; ls -1 Rom 2>/dev/null | wc -l
	@$(MAKE) --no-print-directory check
	@echo ""
	@echo "  Lee CLAUDE.md para el contrato y docs/08-continuar-por-consola.md"
	@echo "  para el flujo de trabajo desde terminal."

check:
	@$(PY) -c "import sys; sys.path.insert(0,'tools'); import psxiso, acepack, psexe, ulz, acerec, tim2png, acemodel, acesnd, acebin, aceovl; print('herramientas OK')"

psx-check:
	@mkdir -p psx/build
	@cc -O2 -Wall -Wextra -Ipsx/include psx/test/test_gte.c psx/src/gte.c -lm -o psx/build/test_gte
	@psx/build/test_gte
	@cc -O2 -Wall -Wextra -Ipsx/include psx/test/test_gpu.c psx/src/gpu.c psx/src/mem.c -o psx/build/test_gpu
	@$(PY) tools/timdump.py data/psx_tims >/dev/null
	@psx/build/test_gpu data/psx_tims/*.tim | tail -1
	@if [ -f data/bench/mision.gpu ]; then cc -O2 -Ipsx/include psx/test/bench_gpu.c psx/src/gpu.c psx/src/mem.c -o psx/build/bench_gpu && psx/build/bench_gpu data/bench/mision.gpu 50; fi
	@cc -O2 -Wall -Wextra -Wno-unused-parameter -Ipsx/include psx/test/test_etc.c psx/src/libetc.c psx/src/libcd.c psx/src/libpad.c psx/src/libapi.c psx/src/mem.c -o psx/build/test_etc
	@psx/build/test_etc
	@$(PY) tools/psxcheck_io.py "$(EU)" data/exe/ace_eu.bin
	@$(PY) tools/psyqcheck_spu.py data/exe/ace_eu.bin
	@$(PY) tools/psyqcheck_libc.py data/exe/ace_eu.bin
	@$(PY) tools/psxcheck_snd.py data/exe/ace_eu.bin
	@$(PY) tools/psxcheck_xa.py "$(EU)"
	@$(PY) tools/psyqcheck_press.py "$(EU)" data/exe/ace_eu.bin 8
	@$(PY) tools/psxcheck_mdec.py "$(EU)" 4
	@$(PY) tools/psxcheck_card.py data/exe/ace_eu.bin data/exe/ovl/overlay1.exe
	@$(MAKE) -s psx-display
	@$(PY) tools/psyqcheck.py data/exe/ace_eu.bin
	@$(PY) tools/psyqcheck_gte.py data/exe/ace_eu.bin 300
	@$(PY) tools/psyqcheck_gpu.py data/exe/ace_eu.bin 500

psyq-api:
	@$(PY) tools/psyqapi.py "$(GHIDRA)/Ghidra/Extensions/ghidra_psx_ldr/data/psyq/450" data/ghidra/all_ace_eu.c data/ghidra/all_overlay1.c data/ghidra/all_overlay2.c data/ghidra/all_overlay3.c data/ghidra/all_overlay4.c data/ghidra/all_overlay5.c

psx-scene:
	@mkdir -p psx/build
	@cc -O2 -Wall -Ipsx/include -Iandroid/src psx/test/test_scene.c psx/src/*.c android/src/ace.c -lm -o psx/build/test_scene
	@psx/build/test_scene "$(EU)" 6 data/psx_scene.ppm 14000 -2200 -14000 3584 -360
	@echo "imagen: data/psx_scene.ppm"

progress:
	@$(PY) tools/progress.py count "$(GHIDRA)"

# cabeceras de EGL/GLES del NDK para las pruebas de escritorio (se enlaza con Mesa)
NDK ?= $(HOME)/tools/android-sdk/ndk/30.0.16248370
GLINC = android/build/inc
GLLIBS = /usr/lib/x86_64-linux-gnu/libEGL.so.1 /usr/lib/x86_64-linux-gnu/libGLESv2.so.2

$(GLINC)/GLES3/gl3.h:
	@mkdir -p $(GLINC)
	@cp -r $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/EGL $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/GLES3 $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/KHR $(GLINC)/

psx-display: $(GLINC)/GLES3/gl3.h
	@mkdir -p psx/build
	@cc -O2 -Wall -Wextra -Wno-unused-parameter -Ipsx/include -I$(GLINC) psx/test/test_display.c psx/gl/psx_present.c psx/src/*.c $(GLLIBS) -lm -o psx/build/test_display
	@psx/build/test_display
	@$(PY) tools/psxcheck_display.py "$(EU)" 4

# etapa 6, paso 7: dibujo por GLES frente a la GPU por software en un fotograma real
# (data/bench/mision.gpu, de PSX_GPU_DUMP); salida en psx/build/hw_{sw,x1,x4}.ppm
psx-hw: $(GLINC)/GLES3/gl3.h
	@mkdir -p psx/build
	@cc -O2 -Wall -Wextra -Wno-unused-parameter -Ipsx/include -I$(GLINC) psx/test/test_hw.c psx/gl/psx_hw.c psx/src/gpu.c psx/src/mem.c $(GLLIBS) -lm -o psx/build/test_hw
	@psx/build/test_hw data/bench/mision.gpu psx/build/hw
	@cc -O2 -Wall -Wextra -Wno-unused-parameter -Ipsx/include -I$(GLINC) psx/test/test_hwlines.c psx/gl/psx_hw.c psx/src/gpu.c psx/src/mem.c $(GLLIBS) -lm -o psx/build/test_hwlines
	@psx/build/test_hwlines

psx-android:
	@rm -rf psx/build/android && mkdir -p psx/build/android
	@for f in psx/src/*.c psx/gl/psx_present.c; do $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang -O2 -fPIC -Wall -Wextra -Wno-unused-parameter -Ipsx/include -c $$f -o psx/build/android/$$(basename $$f .c).o || exit 1; done
	@$(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar rcs psx/build/android/libpsx.a psx/build/android/*.o
	@echo "psx/build/android/libpsx.a (arm64) compilada"

# etapa 6, paso 3: recompilacion del juego (el C generado va a data/recomp, fuera de git)
OVLS = data/exe/ovl/overlay1.exe,data/exe/ovl/overlay2.exe,data/exe/ovl/overlay3.exe,data/exe/ovl/overlay4.exe,data/exe/ovl/overlay5.exe
recomp:
	@$(PY) tools/recomp/recomp.py data/exe/ace_eu.bin data/ghidra/all_ace_eu.c data/recomp --from $(OVLS)
	@for k in 1 2 3 4 5; do $(PY) tools/recomp/recomp.py data/exe/ovl/overlay$$k.exe data/ghidra/all_overlay$$k.c data/recomp --overlay $$k --resident data/recomp/ace_eu.json; done
	@cc -O1 -fPIC -shared -w -Ipsx/include -Ipsx/recomp data/recomp/ace_eu.c data/recomp/overlay[1-5].c psx/recomp/recomp_rt.c psx/src/*.c -lm -o data/recomp/librecomp.so
	@echo "data/recomp/librecomp.so"

# etapa 7: biblioteca con el interprete MIPS en C y sin codigo traducido (solo codigo nuestro)
mipsi:
	@mkdir -p psx/build
	@cc -O2 -fPIC -shared -w -Ipsx/include -Ipsx/recomp psx/recomp/mipsi.c psx/recomp/recomp_rt.c psx/src/*.c -lm -lpthread -o psx/build/libmipsi.so
	@echo "psx/build/libmipsi.so"

# etapa 7: el juego en el PC SOLO con codigo del repositorio (interprete, ACE.BIN del disco,
# version detectada): lo mismo que hara el APK distribuible. DISCO=$(EU) o $(US)
DISCO ?= $(EU)
LIBRE_GUION ?= $(GUION)
libre: libre-build
	@psx/build/ac3_libre "$(DISCO)" - 3600 psx/build/libre 100 "$(LIBRE_GUION)"

# etapa 8: la segunda copia del interprete (simbolos rrw_*) para redibujar en un hilo de dibujo
RR_CLONE = psx/recomp/mipsi.c psx/recomp/recomp_rt.c psx/recomp/ac3_versions.c psx/recomp/ac3_rerender.c psx/recomp/hle_eu.c \
	psx/recomp/hle_us.c psx/src/mem.c psx/src/gte.c psx/src/libgte.c psx/src/libgpu.c
libre-build: $(GLINC)/GLES3/gl3.h
	@mkdir -p psx/build/libre
	@sh tools/rr_clone.sh "cc -O2 -w -D_GNU_SOURCE -DAC3_LIBRE -Ipsx/include -Ipsx/recomp" nm objcopy psx/build/rrw $(RR_CLONE)
	@cc -O2 -w -D_GNU_SOURCE -DAC3_LIBRE -Ipsx/include -Ipsx/recomp -I$(GLINC) psx/recomp/native_main.c psx/recomp/mipsi.c \
		psx/recomp/recomp_rt.c psx/recomp/ac3_versions.c psx/recomp/ac3_rerender.c psx/recomp/ac3_rr_async.c psx/recomp/hle_eu.c psx/recomp/hle_us.c \
		psx/src/*.c psx/gl/psx_hw.c psx/gl/psx_hwcmp.c psx/build/rrw/*.o $(GLLIBS) -lm -lpthread -o psx/build/ac3_libre

# etapa 6, paso 5: carga una a una las 41 misiones (por defecto en la americana) con el binario
# libre y anota fallos y capturas (tools/misiones.py). MISIONES=0-40, DISCO=$(US)
MISIONES ?= 0-40
# etapa 6, paso 17: dibujo por GLES frente a la GPU por software, lista a lista, en las 41
# misiones (tools/hwcmp.py); HWCMP_INTERP=1 compara tambien las imagenes intermedias
HWCMP_ENV = $(if $(HWCMP_INTERP),AC3_INTERP=3 AC3_INTERP_THREADS=3,)
hwcmp: libre-build
	@$(HWCMP_ENV) $(PY) tools/hwcmp.py "$(US)" psx/build/hwcmp $(MISIONES) 4

# etapa 6, paso 18: cada orden de la GTE comparada con la de Mednafen (Beetle PSX, GPL: se descarga
# a data/ref, fuera de git) en las misiones MISIONES (psx/test/gteref.c, tools/gteref.sh)
gteref:
	@sh tools/gteref.sh
	@for m in $$($(PY) -c "import sys; sys.path.insert(0,'tools'); import misiones; print(' '.join(map(str, misiones.parse('$(or $(MISIONES),0-40)'))))"); do \
		mkdir -p psx/build/gteref/$$m; \
		AC3_GTEREF=1 AC3_PAD_ID=73 AC3_MISSION=$$m psx/build/ac3_gteref "$(US)" - 4000 psx/build/gteref/$$m 0 \
			"$$($(PY) -c "import sys; sys.path.insert(0,'tools'); import misiones; print(misiones.GUION)")" | grep "^gteref: [0-9]" | sed "s/^/mision $$m: /"; \
	done

misiones: libre-build
	@$(PY) tools/misiones.py "$(US)" psx/build/misiones $(MISIONES) 2

misiones-eu: libre-build
	@$(PY) tools/misiones.py "$(EU)" psx/build/misiones_eu $(MISIONES) 2 --eu

# etapa 6, paso 13: las secuencias de final y los creditos (tools/finales.py)
finales: libre-build
	@$(PY) tools/finales.py "$(US)" psx/build/finales
finales-eu: libre-build
	@$(PY) tools/finales.py "$(EU)" psx/build/finales_eu --eu

# guardar y cargar partida en la tarjeta (tools/guardado.py); cada vez en una carpeta nueva
guardado: libre-build
	@$(PY) tools/guardado.py "$(US)" psx/build/guardado_$$(date +%s)
guardado-eu: libre-build
	@$(PY) tools/guardado.py "$(EU)" psx/build/guardado_eu_$$(date +%s) --eu

# el interprete en C frente al de referencia, en las mismas entradas que el codigo traducido
mipsi-check: mipsi
	@$(PY) tools/recomp/check.py data/exe/ace_eu.bin data/recomp 24 --interp psx/build/libmipsi.so
	@for k in 1 2 3 4 5; do $(PY) tools/recomp/check.py data/exe/ace_eu.bin data/recomp --overlay $$k data/exe/ovl/overlay$$k.exe 24 --interp psx/build/libmipsi.so; done

# lo mismo en la americana (la version de referencia)
mipsi-check-us: mipsi
	@$(PY) tools/recomp/check.py data/exe/ace_us.bin data/recomp_us 24 --interp psx/build/libmipsi.so --region data/recomp_us/region.json
	@for k in 1 2 3 4 5; do $(PY) tools/recomp/check.py data/exe/ace_us.bin data/recomp_us --overlay $$k data/exe/ovl_us/overlay$$k.exe 24 --interp psx/build/libmipsi.so --region data/recomp_us/region.json; done

recomp-check:
	@$(PY) tools/recomp/check.py data/exe/ace_eu.bin data/recomp 24
	@for k in 1 2 3 4 5; do $(PY) tools/recomp/check.py data/exe/ace_eu.bin data/recomp --overlay $$k data/exe/ovl/overlay$$k.exe 24; done
	@$(PY) tools/progress.py svg

GUION ?= 320:cruz:6,1100:start:8,1400:cruz:6,1600:cruz:6,1800:cruz:6,2000:cruz:6,2200:cruz:6,2400:cruz:6,2600:cruz:6,2800:cruz:6,3000:cruz:6
# comparador con estados reales: arranque en el interprete, funciones recompiladas reproducidas
recomp-record:
	@$(PY) tools/recomp/record.py "$(EU)" 3000 3 "$(GUION)"

# juego recompilado nativo en el PC (sin ventana: capturas en data/recomp/native/shots)
native: recomp
	@$(PY) tools/recomp/gen_hle.py data/recomp/hle_gen.c
	@mkdir -p data/recomp/native/obj data/recomp/native/shots
	@for f in ace_eu overlay1 overlay2 overlay3 overlay4 overlay5 hle_gen; do cc -O1 -w -Ipsx/include -Ipsx/recomp -c data/recomp/$$f.c -o data/recomp/native/obj/$$f.o; done
	@for f in psx/src/*.c psx/recomp/recomp_rt.c; do cc -O2 -w -Ipsx/include -Ipsx/recomp -c $$f -o data/recomp/native/obj/$$(basename $$f .c).o; done
	@cc -O2 -w -D_GNU_SOURCE -Ipsx/include -Ipsx/recomp -c psx/recomp/native_main.c -o data/recomp/native/obj/native_main.o
	@cc -rdynamic data/recomp/native/obj/*.o -lm -lpthread -o data/recomp/native/ac3_native
	@data/recomp/native/ac3_native "$(EU)" data/exe/ace_eu.bin 3600 data/recomp/native/shots 100 "$(GUION)"

# ---- version americana (SLUS_009.72): mismas herramientas, direcciones de tools/recomp/region.py ----
OVLS_US = data/exe/ovl_us/overlay1.exe,data/exe/ovl_us/overlay2.exe,data/exe/ovl_us/overlay3.exe,data/exe/ovl_us/overlay4.exe,data/exe/ovl_us/overlay5.exe
# proyecto de Ghidra fuera del arbol si la ruta lleva un ".": Ghidra no admite esos elementos
GHIDRA_US_PROJ ?= $(CURDIR)/data/ghidra_us
us-prep:
	@mkdir -p data/exe data/bpb_us data/recomp_us
	@$(PY) tools/acebin.py "$(US)" data/exe/ace_us.bin
	@$(PY) tools/acebin.py "$(US)" data/exe/ace_us.exe --psexe
	@test -f data/exe/ace_eu.bin || $(PY) tools/acebin.py "$(EU)" data/exe/ace_eu.bin
	@$(PY) tools/acepack.py "$(US)" extract BPB data/bpb_us | tail -1
	@$(PY) tools/aceovl.py extract data/exe/ace_us.bin data/bpb_us data/exe/ovl_us --table auto
	@$(PY) tools/recomp/region.py data/exe/ace_us.bin data/exe/ace_eu.bin data/recomp_us/region.json data/recomp_us/ac3_region.h --disc "$(US)"

ghidra-us:
	@mkdir -p data/ghidra_us "$(GHIDRA_US_PROJ)"
	@$(HEADLESS) "$(GHIDRA_US_PROJ)" ac3us -import $(CURDIR)/data/exe/ace_us.exe -overwrite -max-cpu 2 \
		-scriptPath $(CURDIR)/tools/ghidra -postScript CreateMissingFunctions.java \
		-postScript FixFunctionStarts.java $$($(PY) -c "import json; print(' '.join('0x%08X' % a for a in json.load(open('data/recomp_us/region.json'))['fix_starts']))") \
		-postScript DumpAll.java $(CURDIR)/data/ghidra_us/all_ace_us.c > data/ghidra_us/import_us.log 2>&1
	@for k in 1 2 3 4 5; do $(HEADLESS) "$(GHIDRA_US_PROJ)" ac3us -import $(CURDIR)/data/exe/ovl_us/overlay$$k.exe -overwrite -max-cpu 2 \
		-scriptPath $(CURDIR)/tools/ghidra -postScript CreateMissingFunctions.java \
		-postScript DumpAll.java $(CURDIR)/data/ghidra_us/all_overlay$$k.c > data/ghidra_us/import_ovl$$k.log 2>&1; done
	@grep -h "decompiladas" data/ghidra_us/*.log | sed 's/.*> //'

recomp-us:
	@$(PY) tools/recomp/recomp.py data/exe/ace_us.bin data/ghidra_us/all_ace_us.c data/recomp_us --from $(OVLS_US) --region data/recomp_us/region.json
	@for k in 1 2 3 4 5; do $(PY) tools/recomp/recomp.py data/exe/ovl_us/overlay$$k.exe data/ghidra_us/all_overlay$$k.c data/recomp_us --overlay $$k --resident data/recomp_us/ace_us.json --region data/recomp_us/region.json; done
	@cc -O1 -fPIC -shared -w -Ipsx/include -Ipsx/recomp data/recomp_us/ace_us.c data/recomp_us/overlay[1-5].c psx/recomp/recomp_rt.c psx/src/*.c -lm -o data/recomp_us/librecomp.so
	@echo "data/recomp_us/librecomp.so"

recomp-check-us:
	@$(PY) tools/recomp/check.py data/exe/ace_us.bin data/recomp_us 24 --region data/recomp_us/region.json
	@for k in 1 2 3 4 5; do $(PY) tools/recomp/check.py data/exe/ace_us.bin data/recomp_us --overlay $$k data/exe/ovl_us/overlay$$k.exe 24 --region data/recomp_us/region.json; done

# guion del mando de la US (NTSC, 60 VBlank/s; sin menu de idioma): ver docs/11-etapa6.md
GUION_US ?= 300:start:8,500:cruz:6,700:cruz:6,900:cruz:6,1100:cruz:6,1300:cruz:6,1500:cruz:6,1700:cruz:6,1900:cruz:6,2100:cruz:6,2300:cruz:6,2500:cruz:6,3100:start:6,3500:cruz:6,3700:cruz:6,3900:cruz:6,4100:cruz:6
FRAMES_US ?= 4300
native-us: recomp-us native-us-build
	@data/recomp_us/native/ac3_native "$(US)" data/exe/ace_us.bin $(FRAMES_US) data/recomp_us/native/shots 100 "$(GUION_US)"

native-us-build:
	@$(PY) tools/recomp/gen_hle.py data/recomp_us/hle_gen.c --region data/recomp_us/region.json data/exe/ace_us.bin data/exe/ovl_us data/ghidra_us/all_ace_us.c
	@mkdir -p data/recomp_us/native/obj data/recomp_us/native/shots
	@for f in ace_us overlay1 overlay2 overlay3 overlay4 overlay5 hle_gen; do cc -O1 -w -Ipsx/include -Ipsx/recomp -c data/recomp_us/$$f.c -o data/recomp_us/native/obj/$$f.o; done
	@for f in psx/src/*.c psx/recomp/recomp_rt.c; do cc -O2 -w -Ipsx/include -Ipsx/recomp -c $$f -o data/recomp_us/native/obj/$$(basename $$f .c).o; done
	@cc -O2 -w -D_GNU_SOURCE -Ipsx/include -Ipsx/recomp -include data/recomp_us/ac3_region.h -c psx/recomp/native_main.c -o data/recomp_us/native/obj/native_main.o
	@cc -rdynamic data/recomp_us/native/obj/*.o -lm -lpthread -o data/recomp_us/native/ac3_native
