# Investigación: respuesta de inquiry scan (ser descubrible)

Estado del adaptador 3DSP portado (ver PORTING-6.12.md para el overview):

| Función | Estado |
|---|---|
| Driver cargado en kernel 6.12, hci0 + BlueZ | ✅ |
| Escaneo saliente (inquiry TX/RX) | ✅ |
| Emparejamiento saliente + link key + A2DP audio | ✅ |
| **Ser descubierto (FHS de inquiry scan)** | ❌ |

## Cadena de arranque real (descifrada por ingeniería inversa)

1. `BtStartDevice` descarga 8051 + DSP (CardID 0300) y levanta `hci0`,
   pero el 8051 queda en su **"min process"**: el baseband no está
   activo todavía.
2. El 8051 debe anunciar por el endpoint INT un paquete de tipo
   **`0x0d` (WAIT)** con un byte indicando si existe WLAN (`0x02`) o no
   (`0x00`). Con `0x00`, el driver lanza la **fase 2**
   (`BT_TASK_EVENT_START_STAGE_MAINLOOP` → `BtStartDevice_2Phase(mode=1)`):
   recarga el código DSP y envía `VCMD_API_8051_JUMP_MAIN_PROCESS`
   esperando `MainLoopEvent`. Solo entonces el DSP ejecuta su proceso
   principal y el baseband responde (page/iscan/ACL).
3. **Ese INT `0x0d` nunca se recibe** (probablemente se emite mientras
   el URB de INT todavía no está enviado, justo tras la descarga) → la
   fase 2 nunca arrancaba → DSP atrapado en min-process → iscan muerto.

### Fix aplicado (bt_pnp.c)

Al final de `BtStartDevice` se dispara manualmente la tarea
`START_STAGE_MAINLOOP` con mode=1, y se deja `StartStage` en INIT para
que la tarea pase el gate. Tras el fix: la fase 2 se ejecuta, el 8051
entra en main process (evidencia: el registro indicador
`BT_REG_HCI_COMMAND_INDICATOR` pasa a contener bits escritos por el
propio 8051 — `EXIT_SNIFF`, `READ_CLOCK` — mezclados con los del
driver; y el contador `RX acl` pasa de 0 a tráfico real).

**Resultado: el baseband está vivo pero el FHS de inquiry scan sigue
sin transmitirse.** Reaplicar scan-enable/ventana tras la recarga no
bastó. Pendiente: comprobar `BT_REG_INQUIRY_SCAN_WINDOW` (0x2191) tras
la fase 2, y el contenido exacto de los parámetros iniciales del DSP
(Port 21 "BT DSP initial parameter" de `3dspcode.bin`).

## Hechos verificados (para no repetir trabajo)

- El driver open YA maneja el INT `0x40`
  (`DSP_SCRATCH_SPACE_INFO = 0x40` en btusb.h → `Frag_ProcessSpaceInt`);
  el driver privativo hace exactamente lo mismo (verificado por
  desensamblado de la jump table de `UsbProcessInt` en `3dspusbbtpriv.o`).
- La jump table del priv cubre tipos 0x00–0x40; tipos > 0x40 solo se
  vuelcan con `BtPrintBuffer` y se ignoran.
- El blob `3dspusbbtpriv.o` **no está stripped y lleva DWARF**: se puede
  mapear cada instrucción a su fichero/línea de fuente
  (`/home/carl/桌面/xlu_10_0817/driver_src/private/new_bluetooth_priv/lib/btusb.c`).
- `nm` del priv vs fuentes open: **no falta ninguna función con lógica
  propia** — solo las capas `Bluetooth_Adap_*` (wrapper de adaptador
  reemplazado por `hci_usb_probe/open` en el port) y shims `sc_*`.
- `BtWriteFHSPacket` (open, bt_sendrecv.c) es para PAGING (escribe
  `BT_REG_FHS_FOR_PAGE`); el FHS de inquiry scan se escribe una vez en
  `PostInit` (`BT_REG_FHS_FOR_INQUIRY_SCAN`) y se verificó por
  relectura.
- Indicador decodificado: `BIT_16`=WRITE_INQUIRY_SCAN_EN,
  `BIT_24`=WRITE_PAGE_SCAN_EN, `BIT_25`=READ_CLOCK,
  `BIT_10`=EXIT_SNIFF (bt_hal.h).
- El 8051 sigue vivo a través de rmmod/insmod: para re-descargar hay
  que resetear el USB (`USBDEVFS_RESET`, herramienta `usbreset`).
- Los dos firmwares probados (`holtmann` y la release completa de
  Alamot `3.0.4_100820`) dan el mismo comportamiento.
- En modo combo (WLAN enchufado primero) el 8051 no responde el
  `MAILBOX_CMD_REQUEST_TO_JOIN` del BT → `BtStartDevice` falla en
  STEP=6. El join del WLAN sí se completa ("join int returned").
- La lectura de `BT_REG_DEBUG0` (0x2338) devuelve un patrón constante
  `0x2726251f` — o el registro no existe en esta revisión del DSP o
  está en otra dirección (el mapa de bt_hal.h tiene juegos alternativos
  bajo `#if 0`).

## Herramientas de análisis creadas

- `usbtest.c`: control transfer cruda por usbfs (verifica ep0 vivo).
- `usbreset` (ver INSTALL de bt-audio): `USBDEVFS_RESET` del chip.
- Python one-liners para extraer la jump table de `UsbProcessInt`
  desde las reubicaciones R_386_32 contra `.rodata` (addend embebido
  en los bytes del disp32, offset archivo = vaddr + 0x40 por el
  desplazamiento de la sección .text).

## Siguientes pasos concretos

1. Leer `BT_REG_INQUIRY_SCAN_WINDOW` (0x2191) y `PAGE_SCAN_WINDOW`
   tras la fase 2; si son 0, escribir 0x12/0x800 y retest.
2. Volcar y comparar los "BT DSP initial parameter" (Port 21) del
   firmware contra lo que el priv escribe (hay una función
   `BtInitDSPParameters` con símbolos en el priv).
3. Con el DWARF del priv: reconstruir `Hci_CoordinateDspState` y el
   watchdog (`BtWatchdogTimer`) del priv para ver qué corrige el
   estado DSP cada segundo.
4. Probar un dongle USB BT externo en la misma máquina como control
   positivo del setup de escaneo del Pi.
