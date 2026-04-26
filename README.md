# Digivice Arduino

Firmware para ESP32-C3 con pantalla redonda GC9A01 de 240x240, touch CST816D,
LovyanGFX como driver de pantalla y LVGL 8.3.11 como motor de UI.

## Comandos

```bash
pio run
pio run --target upload
pio device monitor
```

Si `pio` no esta en el `PATH`, usa:

```bash
~/.platformio/penv/bin/pio run
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

## Flujo De Pantalla Y Color

`src/main.cpp` define la clase `LGFX`, que configura el bus SPI2 y el panel
`Panel_GC9A01`. LVGL renderiza en dos buffers RGB565 de 240 x 60 lineas y llama
`my_disp_flush()` cuando necesita enviar un area actualizada a pantalla.

La ruta de color actual es:

1. LVGL genera pixeles `lv_color_t` con `LV_COLOR_DEPTH 16`.
2. `LV_COLOR_16_SWAP` queda en `0`, por lo que el buffer queda en RGB565 nativo.
3. `my_disp_flush()` entrega ese buffer a LovyanGFX como `lgfx::rgb565_t`.
4. LovyanGFX convierte al orden de bytes que necesita el GC9A01 por SPI.

Los ajustes del panel estan agrupados en `src/main.cpp`:

```cpp
static constexpr uint32_t TFT_WRITE_HZ = 40000000;
static constexpr bool PANEL_INVERT_COLOR = false;
static constexpr bool PANEL_BGR_ORDER = true;
```

Si el panel muestra colores negativos, cambia `PANEL_INVERT_COLOR`. Si rojo y
azul aparecen intercambiados, cambia `PANEL_BGR_ORDER`.

## Demo

`create_ui()` arma una demo tipo Digivice optimizada para la pantalla redonda:

- Tres modos: `CORE`, `SCAN` y `LINK`.
- Toque en pantalla para avanzar al siguiente modo.
- Anillos `lv_arc` con telemetria simulada.
- Nucleo central pixel-art generado en RAM como imagen LVGL con chroma-key.
- Barras compactas de estado con `lv_bar`.
- Chart inferior con senal cambiante por modo.
- Barrido visual en modo `SCAN`.

La logica dinamica vive en `demo_timer_cb()` y `update_demo_values()`. El timer
corre cada 120 ms y actualiza arcos, barras, chart, textos y puntos orbitales.

El touch se registra como `LV_INDEV_TYPE_POINTER` y usa `CST816D::getTouch()`.

## Configuracion Importante

- `platformio.ini` fija `LovyanGFX@1.2.19`. No usar `^1.2.19`: instala 1.2.20 y
  rompe compatibilidad con esta combinacion de LVGL.
- `TFT_eSPI` no esta en `lib_deps`; el render real usa LovyanGFX.
- `include/lv_conf.h` habilita los widgets usados por la demo.
- `LV_USE_CHART` debe estar en `1` porque `main.cpp` usa `lv_chart_*`.
- `LV_USE_BAR` debe estar en `1` porque la demo usa indicadores compactos.
- `TFT_WRITE_HZ` esta en 40 MHz para reducir glitches de color en SPI.

## Mapa Del Codigo

| Seccion | Responsabilidad |
| --- | --- |
| Pines y color | Pines SPI/I2C, backlight, frecuencia SPI y orden RGB/BGR |
| `LGFX` | Configura bus SPI2 y panel `Panel_GC9A01` |
| `my_disp_flush()` | Entrega buffers RGB565 de LVGL a LovyanGFX por DMA |
| Touch | Conecta `CST816D` como input pointer de LVGL |
| Modelo demo | Temas, nombres de estados y modo activo |
| `build_digicore_img()` | Genera el asset central pixel-art |
| `apply_mode()` | Aplica colores, textos y visibilidad por modo |
| `update_demo_values()` | Simula telemetria y alimenta chart/arcos/barras |
| `create_ui()` | Crea el arbol de widgets y animaciones |

## Diagnostico Rapido

| Sintoma | Primer ajuste |
| --- | --- |
| Colores negativos o lavados | Cambiar `PANEL_INVERT_COLOR` |
| Rojo y azul invertidos | Cambiar `PANEL_BGR_ORDER` |
| Pixeles aleatorios o ruido | Bajar `TFT_WRITE_HZ` a 27000000 |
| UI compila sin chart | Verificar `LV_USE_CHART 1` |
| Barras no compilan | Verificar `LV_USE_BAR 1` |
