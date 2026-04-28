# ESP32-C3 Game Boy Emulator

Este directorio es un port de `esp32-gameboy` para una placa ESP32-C3 con:

- pantalla redonda GC9A01 de 240x240
- touch CST816D
- LovyanGFX como driver de pantalla
- Pokemon Yellow embebido en `src/gbrom.h`
- portal web propio para control remoto y streaming ligero

## Comandos

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run --target upload
~/.platformio/penv/bin/pio device monitor
```

## Documentacion

La documentacion tecnica del proyecto vive en [`docs/README.md`](docs/README.md).

## Arquitectura

| Archivo | Responsabilidad |
| --- | --- |
| `src/main.cpp` | Wrapper minimo de Arduino. |
| `src/emulator_runtime.cpp` | Inicializacion del ROM, memoria, CPU, loop por frame y pacing a 60 FPS. |
| `src/board_config.h` | Pines, dimensiones, paleta DMG y constantes de timing. |
| `src/display.cpp` | Driver GC9A01 con LovyanGFX y conversion framebuffer indexado -> RGB565. |
| `src/apu.cpp` | APU mono opcional y buffer PCM para streaming web. |
| `src/touch_input.cpp` | Mapeo CST816D a botones y direcciones de Game Boy. |
| `src/sdl.cpp` | Fachada compatible con el core original (`sdl_*`). |
| `src/web_portal.cpp` | SoftAP, HTTP, WebSocket, controles web y streaming del framebuffer. |
| `src/mem.cpp` | Mapa de memoria, IO, DMA, bancos ROM y RAM de cartucho. |
| `src/mbc.cpp` | Controladores MBC1, MBC3 y MBC5. |
| `src/cpu.cpp`, `src/lcd.cpp`, `src/timer.cpp`, `src/interrupt.cpp` | Core de emulacion. |

## Optimizaciones Aplicadas

- `platformio.ini` compila con `-O3` y `-DINTER_MODULE_OPT`, ruta ya prevista por
  el core para reducir overhead entre CPU, memoria, timer e interrupciones.
- Los cambios de banco ROM ya no copian 16 KiB a RAM; `mem.cpp` cambia un
  puntero al banco activo. Esto reduce trabajo justo donde Pokemon cambia de
  banco con frecuencia.
- La RAM de cartucho ahora existe como almacenamiento bankeado en RAM para
  MBC1/MBC3/MBC5. Esto permite que juegos como Pokemon usen SRAM durante la
  sesion.
- El render conserva el framebuffer indexado de 2 bits del core y hace una
  conversion lineal a RGB565 solo al presentar el frame.
- La capa de touch y la de pantalla estan separadas, asi que ajustar pines,
  zonas tactiles o el panel no obliga a tocar el core del emulador.

## Portal Web

El dispositivo levanta un punto de acceso propio:

| Campo | Valor |
| --- | --- |
| SSID | `GameBoy-Link` |
| Password | `gameboy123` |
| HTTP | `http://192.168.4.1/` |
| WebSocket | `ws://192.168.4.1:81/` |

La pagina web permite controlar el juego con botones tactiles en el navegador.
Los comandos via WebSocket usan este payload:

```json
{"type":"input","control":"a","pressed":true}
```

`control` puede ser `up`, `down`, `left`, `right`, `a`, `b`, `start` o
`select`. Tambien existe un fallback HTTP:

```bash
curl -X POST http://192.168.4.1/api/input -d "control=a&pressed=1"
curl -X POST http://192.168.4.1/api/input -d "control=a&pressed=0"
curl http://192.168.4.1/api/input_state
```

El stream de pantalla se manda por el mismo WebSocket como frames binarios
empaquetados a 2 bits por pixel. Cada frame pesa ~5.8 KiB y se emite a 10 FPS
por defecto (`WEB_STREAM_INTERVAL_MS = 100`). Esto es deliberadamente mas
ligero que mandar RGB565 completo, que costaria ~46 KiB por frame.

El audio web esta desactivado por defecto porque el APU software consume CPU
visible en el ESP32-C3. Para volver a probarlo, compila con
`-DGB_ENABLE_AUDIO=1` o cambia el default en `src/board_config.h`. Cuando esta
activo, viaja por WebSocket como frames binarios `GBA`: PCM unsigned 8-bit mono
a `11025 Hz`.

El input web se mezcla con el touch fisico y genera interrupcion de joypad al
detectar una nueva pulsacion. La pagina tambien retiene cada tap por un tiempo
minimo, y el firmware aplica otra retencion minima de input para que el juego no
pierda pulsaciones cuando `pressed=true` y `pressed=false` llegan en el mismo
ciclo de red.

El panel inferior muestra `web buttons`, `web directions` y `ff00`. `ff00` es
el valor real del registro joypad que lee el emulador; si cambia al presionar un
boton, el input ya llego hasta el core.

## Audio

La APU actual esta detras del flag `GB_ENABLE_AUDIO` y queda apagada por
defecto. Con `GB_ENABLE_AUDIO=0`, el emulador no ejecuta `apu_cycle()`, no
procesa registros APU en `mem.cpp` y el portal reporta `audio disabled`.

Para habilitarla temporalmente:

```ini
build_flags =
	-DGB_ENABLE_AUDIO=1
```

Cuando esta habilitada, la APU maneja writes/reads en
`FF10`-`FF3F`, genera muestras a partir de pulse 1/2, wave y noise, y aplica
length counters y envolvente basica. No es todavia una APU cycle-perfect:

- El sweep de canal 1 esta simplificado.
- La mezcla stereo de `NR50/NR51` se reduce a mono.
- La salida fisica por PWM/I2S no esta conectada; de momento el destino es web.

Para audio local en el dispositivo, la ruta recomendada sigue siendo PWM/LEDC
mono con filtro/amplificador simple, o I2S con DAC externo si se agrega hardware.

## Hardware

| Senal | GPIO |
| --- | --- |
| TFT SCLK | 6 |
| TFT MOSI | 7 |
| TFT CS | 10 |
| TFT DC | 2 |
| Backlight | 3 |
| Touch SDA | 4 |
| Touch SCL | 5 |
| Touch INT | 0 |
| Touch RST | 1 |

## Notas

- El framebuffer visible se dibuja centrado en 160x144 dentro de la pantalla
  redonda de 240x240.
- Las zonas tactiles reemplazan el gamepad fisico original.
- La SRAM de cartucho todavia no se persiste en flash; funciona durante la
  sesion actual, pero los saves se pierden al reiniciar.
- Este port sigue apuntando a Game Boy/DMG. Los juegos GBC-only requieren una
  ruta de video y memoria CGB adicional.
- Activar Wi-Fi sube el uso de RAM y puede afectar el pacing del emulador si se
  aumenta demasiado la frecuencia del stream.
- Si se habilita el audio web, el navegador requiere una interaccion del usuario
  para desbloquear Web Audio.
