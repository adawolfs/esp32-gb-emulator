# ESP32-C3 Emulator

Este directorio es un port de `esp32-gameboy` para una placa ESP32-C3 con:

- pantalla redonda GC9A01 de 240x240
- touch CST816D
- build con PlatformIO
- LovyanGFX como driver de pantalla

El core de emulacion en `src/` debe reflejar el codigo del repositorio raiz.
Las unicas diferencias esperadas son las capas de adaptacion a esta placa:

- `src/main.cpp`: wrapper Arduino `setup()/loop()`
- `src/sdl.cpp`: salida de video y mapeo de touch a botones Game Boy
- `src/CST816D.cpp` y `src/CST816D.h`: driver del touch

## Comandos

```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio run --target upload
~/.platformio/penv/bin/pio device monitor
```

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

- El framebuffer del emulador sigue siendo indexado de 2 bits, igual que en la
  implementacion base.
- `src/sdl.cpp` traduce ese framebuffer a RGB565 para el panel GC9A01.
- Las zonas tactiles reemplazan el gamepad fisico original del proyecto raiz.
