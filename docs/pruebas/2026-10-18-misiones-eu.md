# Prueba: las 41 misiones, una a una (versión europea) — 2026-10-18

La misma prueba que [la americana](2026-10-18-misiones-us.md), pedida por el
dueño para documentar las dos versiones.

| | |
|---|---|
| Disco | Europeo, `SCES-02066` (prueba de regresión: la referencia es la americana) |
| Ejecutable | `psx/build/ac3_libre` (`make libre-build`), el código del APK |
| Herramienta | `tools/misiones.py … --eu` (`make misiones-eu`) |
| Cómo se elige la misión | `AC3_MISSION=N` en la función que arranca la misión (EU `0x800355F0`, misión actual en `g_state + 0x390`, con `g_state` en `0x800BACD0`). El juego la arranca en el fotograma 3281 de este guion |
| Partida | 5.100 fotogramas (102 s de juego a 50 Hz). El guion europeo, con menú de idioma, llega a la misión y después vuela a ciegas con las palancas y dispara misiles |
| Qué se comprueba | Salida normal, **0 fallos** `rc_fault` y, en las capturas cada 300 fotogramas, que la misión dibuja su escenario y se vuela |
| Qué NO se comprueba | Completar misiones, finales, guardado y DuckStation |

## Resultado: 41/41 sin fallos

Todas terminan con salida 0 y 0 fallos. En las hojas de contactos
(`psx/build/misiones_eu/hoja1-4.png`) cada misión muestra el mismo escenario
que en la americana: ciudad y costa, montañas, mar de noche, cañón, nieve,
despegue desde pista, túneles del Geofront, el espacio, la Electrosfera…
"MISSION FAILED" y "Replay" se deben al guion, que vuela sin mirar.

Diferencias de comportamiento con la americana: **ninguna en lo comprobado**.

- **16:** secuencia del lanzamiento. El juego encadena la **17** y se vuela
  en el espacio.
- **37:** "ENTERING ELECTROSPHERE".
- **38:** tras los mensajes del desenlace ("…commended. Nice going!") pasa a
  una escena con el avión en vuelo libre.

**Reproducir:** `make misiones-eu` (`MISIONES=…`).
