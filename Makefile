# Atajos del proyecto. Requiere solo Python 3.
PY  ?= python3
EU  := Rom/Ace Combat 3 - Electrosphere (Europe) (EnFrDeEsIt)/Ace Combat 3 - Electrosphere (Europe) (En,Fr,De,Es,It).bin
US  := Rom/Ace Combat 3 - Electrosphere (USA)/Ace Combat 3 - Electrosphere (USA).bin
DISC ?= $(EU)

.PHONY: help brief files info index inventory records tags extract-bpb ulz-check sections textures models sounds check

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
	@echo "make check      - comprueba que las herramientas corren"
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

sounds:
	@$(PY) tools/acesnd.py check data/bpb

textures:
	@mkdir -p data/sec data/png
	@$(PY) tools/acerec.py dump data/bpb/$(REC)_*.bin data/sec
	@for f in data/sec/$(REC)_*_s*.bin; do $(PY) tools/tim2png.py png "$$f" data/png; done

extract-bpb:
	@mkdir -p data/bpb
	@$(PY) tools/acepack.py "$(DISC)" extract BPB data/bpb

brief:
	@echo "=============================================================="
	@echo " Ace Combat 3: Electrosphere - ingenieria inversa y port"
	@echo "=============================================================="
	@echo ""
	@sed -n '1,/^## Sin empezar/p' STATUS.md | sed '/^## Sin empezar/d'
	@echo "--------------------------------------------------------------"
	@echo "ENTORNO"
	@$(PY) -c "import sys; print('  python', sys.version.split()[0])"
	@printf "  ghidra: "; command -v ghidra >/dev/null && echo "instalado" || echo "NO instalado (necesario en la etapa 3)"
	@printf "  discos en Rom/: "; ls -1 Rom 2>/dev/null | wc -l
	@$(MAKE) --no-print-directory check
	@echo ""
	@echo "  Lee CLAUDE.md para el contrato y docs/08-continuar-por-consola.md"
	@echo "  para el flujo de trabajo desde terminal."

check:
	@$(PY) -c "import sys; sys.path.insert(0,'tools'); import psxiso, acepack, psexe, ulz, acerec, tim2png, acemodel, acesnd; print('herramientas OK')"
