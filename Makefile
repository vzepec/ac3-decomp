# Atajos del proyecto. Requiere solo Python 3.
PY  ?= python3
GHIDRA ?= $(HOME)/tools/ghidra_12.1.4_PUBLIC
JDK    ?= $(HOME)/tools/jdk-21.0.12.1+1
HEADLESS = JAVA_HOME="$(JDK)" PATH="$(JDK)/bin:$$PATH" "$(GHIDRA)/support/analyzeHeadless"
EU  := Rom/Ace Combat 3 - Electrosphere (Europe) (EnFrDeEsIt)/Ace Combat 3 - Electrosphere (Europe) (En,Fr,De,Es,It).bin
US  := Rom/Ace Combat 3 - Electrosphere (USA)/Ace Combat 3 - Electrosphere (USA).bin
DISC ?= $(EU)

.PHONY: help brief files info index inventory records tags extract-bpb ulz-check sections textures models sounds acebin overlays apk ghidra-import ghidra-stats check psx-check psyq-api psx-scene

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
	@cc -O2 -Wall -Wextra -Wno-unused-parameter -Ipsx/include psx/test/test_etc.c psx/src/libetc.c psx/src/libcd.c psx/src/libpad.c psx/src/mem.c -o psx/build/test_etc
	@psx/build/test_etc
	@$(PY) tools/psxcheck_io.py "$(EU)" data/exe/ace_eu.bin
	@$(PY) tools/psyqcheck_spu.py data/exe/ace_eu.bin
	@$(PY) tools/psxcheck_snd.py data/exe/ace_eu.bin
	@$(PY) tools/psxcheck_xa.py "$(EU)"
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
