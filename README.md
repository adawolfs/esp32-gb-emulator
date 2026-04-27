# ESP32-C3 Game Boy Emulator

Este directorio es un port de `esp32-gameboy` para una placa ESP32-C3 con:

- pantalla redonda GC9A01 de 240x240
- touch CST816D
- LovyanGFX como driver de pantalla
- Pokemon Yellow embebido en `src/gbrom.h`

## Comandos

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run --target upload
~/.platformio/penv/bin/pio device monitor
```

## Arquitectura

| Archivo | Responsabilidad |
| --- | --- |
| `src/main.cpp` | Wrapper minimo de Arduino. |
| `src/emulator_runtime.cpp` | Inicializacion del ROM, memoria, CPU, loop por frame y pacing a 60 FPS. |
| `src/board_config.h` | Pines, dimensiones, paleta DMG y constantes de timing. |
| `src/display.cpp` | Driver GC9A01 con LovyanGFX y conversion framebuffer indexado -> RGB565. |
| `src/touch_input.cpp` | Mapeo CST816D a botones y direcciones de Game Boy. |
| `src/sdl.cpp` | Fachada compatible con el core original (`sdl_*`). |
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
