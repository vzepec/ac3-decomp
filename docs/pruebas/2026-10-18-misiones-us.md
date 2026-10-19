# Prueba: las 41 misiones, una a una (versión americana) — 2026-10-18

Registro de la prueba pedida por el dueño del proyecto tras jugar varias
misiones en la Retroid sin errores: **cargar todas las misiones una a una y
comprobar que funcionan.**

## Qué se probó y cómo

| | |
|---|---|
| Disco | Americano, `SLUS-00972` (la versión de referencia desde 2026-10-18) |
| Ejecutable | `psx/build/ac3_libre` (`make libre-build`): el mismo código que el APK publicado (intérprete MIPS + capa `psx/`), con `ACE.BIN` leído del disco |
| Herramienta | `tools/misiones.py` (`make misiones`) |
| Cómo se elige la misión | `AC3_MISSION=N`: al entrar en la función que arranca la misión (US `0x800357B8`), se escribe N en la misión actual del estado de la partida (`g_state + 0x390`). El juego carga entonces `mission_records[N]`, registros 6 a 46 del BPB. Solo se fuerza la primera vez; los encadenamientos los decide el juego |
| Partida | 4.000 fotogramas (66,7 s de juego a 60 Hz). Guion: botones de los menús hasta la misión y después vuelo a ciegas con la palanca izquierda, la derecha y misiles |
| Qué se comprueba | Que el proceso termina con normalidad, que no hay **ningún** fallo del código del juego (`rc_fault`: acceso a memoria inválido, instrucción desconocida…) y, en las capturas cada 300 fotogramas, que la misión dibuja su escenario y se vuela |
| Qué NO se comprueba | Completar la misión, los finales, el árbol de la historia, guardar y cargar, y la comparación con DuckStation |

## Resultado: 41/41 sin fallos

Todas las misiones terminaron con salida 0 y **0 fallos**. Las
observaciones salen de las hojas de contactos (`make misiones` →
`psx/build/misiones/hoja1-4.png`). "Se estrella" o "MISSION FAILED" son
consecuencia del guion, que vuela sin mirar, no un fallo.

| Misión | Registro BPB | Resultado | Observaciones (capturas) |
|---|---|---|---|
| 0 | 6 | ✅ | "ENGAGE"; costa y ciudad; se estrella |
| 1 | 7 | ✅ | Primera misión de la campaña (la que el juego arranca en una partida nueva). Antes, texto de la historia "PEACEMAKER", que se salta con Start; después "ENGAGE" y vuelo sobre montañas |
| 2 | 8 | ✅ | "ENGAGE"; montañas y volcán |
| 3 | 9 | ✅ | Mensajes de radio ("Commence training flight…"), "PURSUE"; mar y costa |
| 4 | 10 | ✅ | Mar de noche |
| 5 | 11 | ✅ | "ENGAGE"; cielo cubierto |
| 6 | 12 | ✅ | "LASER TARGETING"; noche, "CAUTION / PULL UP" |
| 7 | 13 | ✅ | Escena de inicio; cañón de noche |
| 8 | 14 | ✅ | "ENGAGE"; atardecer |
| 9 | 15 | ✅ | "ENGAGE"; noche |
| 10 | 16 | ✅ | Cañón; se estrella y llega a la repetición ("Replay") |
| 11 | 17 | ✅ | "Unidentified fighters closing in…"; montañas nevadas |
| 12 | 18 | ✅ | Escena de inicio con el avión en un hangar o lanzador |
| 13 | 19 | ✅ | "ENGAGE"; niebla |
| 14 | 20 | ✅ | "INTERCEPT"; mar; "MISSION FAILED" |
| 15 | 21 | ✅ | "CLEARED FOR TAKE OFF"; despegue desde pista ("PRESS START BUTTON FOR AUTOPILOT") |
| 16 | 22 | ✅ | Secuencia del lanzamiento al espacio, sin juego. Al acabar, **el juego encadena la 17** (fotograma 3735) |
| 17 | 23 | ✅ | "LEAVING ATMOSPHERE"; vuelo en el espacio sobre la Tierra (también comprobada encadenada desde la 16) |
| 18 | 24 | ✅ | "ENGAGE"; desierto y montañas; "MISSION FAILED" |
| 19 | 25 | ✅ | "ERADICATE VIRUS"; cielo rojo |
| 20 | 26 | ✅ | "INTERCEPT"; cielo |
| 21 | 27 | ✅ | "ENGAGE"; montañas nevadas |
| 22 | 28 | ✅ | "ENGAGE"; nubes y sol |
| 23 | 29 | ✅ | "ENGAGE"; "Preparations complete. ECM jamming activated" |
| 24 | 30 | ✅ | "ENGAGE"; mar |
| 25 | 31 | ✅ | "ENGAGE"; ciudad; "MISSION FAILED" |
| 26 | 32 | ✅ | "ENGAGE"; noche |
| 27 | 33 | ✅ | "INTERCEPT"; ciudad de noche; "MISSION FAILED" |
| 28 | 34 | ✅ | Banquisa de hielo; "MISSION FAILED", "Replay" |
| 29 | 35 | ✅ | Texto de la historia "REVELATION" (Start lo salta); después "ENGAGE" y vuelo |
| 30 | 36 | ✅ | Pista de noche |
| 31 | 37 | ✅ | Escena de inicio; mar; "MISSION FAILED" |
| 32 | 38 | ✅ | Escena nocturna; túneles ("Two unidentified aircraft escaped…") |
| 33 | 39 | ✅ | Túneles y ciudad de noche |
| 34 | 40 | ✅ | Geofront ("Use Geofront's emergency self destruct mechanism…"); "MISSION FAILED", "Replay" |
| 35 | 41 | ✅ | "You failed to destroy the Geofront…"; "ENGAGE"; ciudad de noche |
| 36 | 42 | ✅ | Mensajes sobre la IA; "ENGAGE"; ciudad |
| 37 | 43 | ✅ | "You successfully entered the Electrosphere…": la Electrosfera |
| 38 | 44 | ✅ | Mensajes del desenlace ("…crippled Ouroboros…", "Commendations are in order…") |
| 39 | 45 | ✅ | "CLEARED FOR TAKE OFF"; pista |
| 40 | 46 | ✅ | "Your flight control subsystem has been hacked by the XR-900's AI…"; ciudad |

## Notas

- **Error del arnés durante la prueba:** un bucle de espera con `pgrep -f
  ac3_libre` se encontraba a sí mismo, porque el texto estaba en su propia
  línea de comandos, y no terminaba. `tools/misiones.py` espera a cada
  proceso por su identificador.
- **Reproducir:** `make misiones` (o `MISIONES=16,17 make misiones`).
  Cada misión deja su registro, sus capturas y su `cobertura.txt` en
  `psx/build/misiones/<N>/`.
