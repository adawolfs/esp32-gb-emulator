#include "web_portal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <memory>

#include "board_config.h"
#include "mem.h"
#include "touch_input.h"

#if GB_ENABLE_AUDIO
#include "apu.h"
#endif

namespace {

constexpr char kHtmlPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
  <title>ESP32 Game Boy</title>
  <style>
    :root {
      color-scheme: light;
      --pad-unit: clamp(2.8rem, 9vw, 4.2rem);
      --action-size: clamp(3.6rem, 12vw, 5rem);
      --menu-width: clamp(4.4rem, 15vw, 5.2rem);
      --menu-height: clamp(1.3rem, 4.4vw, 1.55rem);
      --shell-pad-x: clamp(0.95rem, 4vw, 1.35rem);
      --shell-pad-bottom: clamp(1.6rem, 6vw, 2rem);
      --bg-top: #d6dbcf;
      --bg-bottom: #9ca391;
      --shell: #c9ccb9;
      --shell-shadow: #8d9384;
      --shell-edge: #6e7368;
      --bezel: #606675;
      --bezel-dark: #343944;
      --lcd-frame: #7c856e;
      --lcd: #a7b19d;
      --lcd-deep: #55614e;
      --ink: #1d221d;
      --accent: #652b52;
      --accent-soft: #8d6f87;
      --button-dark: #2e3138;
      --button-light: #454955;
      --label: #464d45;
      --speaker: #6f746d;
      --screen-glow: rgba(228, 234, 218, 0.32);
      font-family: "Trebuchet MS", "Gill Sans", "Avenir Next", sans-serif;
      background: var(--bg-bottom);
      color: var(--ink);
    }
    * {
      box-sizing: border-box;
      touch-action: none;
    }
    body {
      margin: 0;
      min-height: 100vh;
      background:
        radial-gradient(circle at top, #eef1e8 0%, var(--bg-top) 22%, transparent 22.5%),
        linear-gradient(180deg, var(--bg-top), var(--bg-bottom));
    }
    main {
      width: min(100%, 31rem);
      margin: 0 auto;
      padding:
        max(0.9rem, env(safe-area-inset-top))
        max(0.85rem, env(safe-area-inset-right))
        max(1.6rem, env(safe-area-inset-bottom))
        max(0.85rem, env(safe-area-inset-left));
    }
    .dmg {
      position: relative;
      padding: 1rem var(--shell-pad-x) var(--shell-pad-bottom);
      border: 1px solid rgba(255, 255, 255, 0.45);
      border-radius: 1.5rem 1.5rem 4.6rem 1.5rem;
      background:
        linear-gradient(145deg, rgba(255, 255, 255, 0.45), rgba(255, 255, 255, 0.05) 26%, transparent 26%),
        linear-gradient(180deg, #d7dbc9, var(--shell) 35%, #bcc0ae 100%);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.75),
        inset 0 -2px 0 rgba(95, 102, 91, 0.28),
        0 20px 40px rgba(58, 64, 54, 0.28);
      overflow: hidden;
    }
    .dmg::after {
      content: "";
      position: absolute;
      inset: auto 1.2rem 0.72rem;
      height: 1px;
      background: rgba(74, 80, 72, 0.22);
    }
    .topline {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 0.7rem;
      color: var(--label);
      text-transform: uppercase;
      letter-spacing: 0.14em;
      font-size: 0.62rem;
      font-weight: 700;
    }
    h1 {
      margin: 0;
      font-family: "Palatino Linotype", "Book Antiqua", Palatino, serif;
      font-size: 1.15rem;
      letter-spacing: 0.04em;
      text-transform: uppercase;
      color: var(--accent);
    }
    .hero {
      margin-bottom: 1rem;
      border-radius: 0.9rem 0.9rem 2.4rem 0.9rem;
      padding: 0.85rem 0.85rem 1rem;
      background:
        linear-gradient(180deg, #7f8594, var(--bezel) 18%, var(--bezel-dark) 100%);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.22),
        inset 0 -1px 0 rgba(0, 0, 0, 0.22);
    }
    .hero-top {
      display: flex;
      align-items: center;
      gap: 0.55rem;
      margin-bottom: 0.6rem;
      color: #dde0e6;
      font-size: 0.62rem;
      letter-spacing: 0.16em;
      text-transform: uppercase;
    }
    .hero-spacer {
      margin-left: auto;
    }
    .battery {
      width: 0.58rem;
      height: 0.58rem;
      border-radius: 999px;
      background: #d64343;
      box-shadow: 0 0 10px rgba(214, 67, 67, 0.55);
      flex: none;
    }
    .screen-wrap {
      position: relative;
      display: grid;
      place-items: center;
      padding: 0.85rem;
      border-radius: 0.45rem;
      background:
        linear-gradient(180deg, #94a087, var(--lcd-frame) 16%, #6f7963 100%);
      box-shadow:
        inset 0 0 0 1px rgba(33, 37, 31, 0.45),
        inset 0 0 0 4px rgba(222, 228, 212, 0.09);
    }
    .screen-wrap::after {
      content: "";
      position: absolute;
      inset: 0.9rem;
      border-radius: 0.2rem;
      background: linear-gradient(135deg, transparent 0 42%, var(--screen-glow) 42% 58%, transparent 58%);
      pointer-events: none;
    }
    canvas {
      display: block;
      width: 100%;
      image-rendering: pixelated;
      aspect-ratio: 160 / 144;
      background: #1f2a1d;
      border: 2px solid #55614e;
      box-shadow:
        inset 0 0 0 1px rgba(230, 236, 221, 0.2),
        0 0 0 1px rgba(28, 31, 27, 0.4);
    }
    .brandline {
      margin: 0.7rem 0 0;
      color: #d6d9e1;
      text-align: center;
      font-size: 0.66rem;
      letter-spacing: 0.14em;
      text-transform: uppercase;
    }
    .brandline strong {
      font-family: "Palatino Linotype", "Book Antiqua", Palatino, serif;
      font-size: 0.86rem;
      letter-spacing: 0.1em;
      color: #e8ebf2;
    }
    .status {
      margin-top: 0.25rem;
      color: var(--accent-soft);
      font-size: 0.72rem;
      text-align: center;
      min-height: 1em;
    }
    .controls {
      display: grid;
      grid-template-columns: minmax(0, 1fr) minmax(0, 0.94fr);
      gap: 1rem clamp(0.45rem, 2.8vw, 1rem);
      align-items: center;
      margin-bottom: 1rem;
    }
    .dpad-shell {
      display: grid;
      place-items: center;
      min-width: 0;
    }
    .dpad {
      position: relative;
      display: grid;
      grid-template-columns: repeat(3, var(--pad-unit));
      grid-template-rows: repeat(3, var(--pad-unit));
      gap: 0;
    }
    .dpad::before,
    .dpad::after {
      content: "";
      position: absolute;
      background: linear-gradient(180deg, #474b54, #1d2128);
      border-radius: 0.55rem;
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.08),
        inset 0 -1px 0 rgba(0, 0, 0, 0.25);
    }
    .dpad::before {
      inset: 1.1rem 0.9rem;
    }
    .dpad::after {
      inset: 0.9rem 1.1rem;
    }
    .dpad-center {
      grid-column: 2;
      grid-row: 2;
      z-index: 1;
      width: 1.2rem;
      height: 1.2rem;
      border-radius: 0.25rem;
      background: #252932;
      box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.05);
    }
    .action-cluster {
      display: flex;
      justify-content: center;
      align-items: center;
      gap: clamp(0.45rem, 2.5vw, 0.85rem);
      transform: rotate(-24deg);
      padding: 0.25rem clamp(0.45rem, 3vw, 0.85rem) 0.1rem 0.15rem;
      justify-self: end;
      width: 100%;
    }
    .menu {
      grid-column: 1 / -1;
      display: flex;
      justify-content: center;
      gap: 1.1rem;
      margin-top: 0.2rem;
    }
    button {
      position: relative;
      z-index: 2;
      border: 0;
      color: #eff1f6;
      font: inherit;
      font-weight: 700;
      user-select: none;
      -webkit-user-select: none;
      -webkit-tap-highlight-color: transparent;
    }
    .dpad button {
      width: 100%;
      height: 100%;
      background: transparent;
      color: #f0f2f7;
      text-shadow: 0 1px 0 rgba(0, 0, 0, 0.35);
      font-size: clamp(0.78rem, 2.8vw, 0.92rem);
      letter-spacing: 0.08em;
      display: grid;
      place-items: center;
      padding: 0;
    }
    .up { grid-column: 2; grid-row: 1; }
    .left { grid-column: 1; grid-row: 2; }
    .right { grid-column: 3; grid-row: 2; }
    .down { grid-column: 2; grid-row: 3; }
    .a,
    .b {
      width: var(--action-size);
      height: var(--action-size);
      border-radius: 999px;
      background: linear-gradient(180deg, #7a3b65, var(--accent) 60%, #451b37);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.18),
        inset 0 -2px 0 rgba(57, 18, 43, 0.5),
        0 5px 0 rgba(91, 95, 87, 0.32);
      color: #f7edf5;
      font-size: clamp(1.05rem, 4vw, 1.3rem);
    }
    .small {
      width: var(--menu-width);
      height: var(--menu-height);
      border-radius: 999px;
      transform: rotate(-24deg);
      background: linear-gradient(180deg, #7a808e, #585d69 58%, #4a4f5a);
      box-shadow:
        inset 0 1px 0 rgba(255, 255, 255, 0.18),
        inset 0 -1px 0 rgba(34, 36, 41, 0.45);
      color: #eff1f4;
      font-size: 0.66rem;
      letter-spacing: 0.08em;
    }
    button[data-held="true"] {
      transform: translateY(2px);
      filter: brightness(1.12);
    }
    .small[data-held="true"] {
      transform: rotate(-24deg) translateY(2px);
    }
    .speaker {
      width: 5.6rem;
      height: 1.7rem;
      margin: 0.25rem 0 0 auto;
      border-radius: 999px;
      background:
        repeating-linear-gradient(90deg,
          transparent 0 0.36rem,
          rgba(255, 255, 255, 0.05) 0.36rem 0.44rem,
          transparent 0.44rem 0.72rem),
        linear-gradient(180deg, rgba(255, 255, 255, 0.08), rgba(54, 60, 55, 0.08));
      transform: rotate(-24deg);
      opacity: 0.92;
    }
    .meta-panel {
      margin-top: 0.8rem;
      padding: 0.75rem 0.85rem;
      border-radius: 0.55rem;
      background: linear-gradient(180deg, #b0bb9b, var(--lcd) 100%);
      box-shadow:
        inset 0 0 0 1px rgba(61, 72, 54, 0.35),
        inset 0 1px 0 rgba(235, 241, 224, 0.5);
    }
    .meta-head {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 0.45rem;
      color: var(--lcd-deep);
      font-size: 0.64rem;
      font-weight: 700;
      letter-spacing: 0.1em;
      text-transform: uppercase;
    }
    pre {
      margin: 0;
      white-space: pre-wrap;
      color: #2b3629;
      font-family: "Courier New", monospace;
      font-size: 0.72rem;
      line-height: 1.38;
    }
    @media (min-width: 760px) {
      main {
        width: min(100%, 42rem);
        padding: 1.4rem 1rem 2rem;
      }
      .dmg {
        padding: 1.2rem 1.25rem 1.8rem;
      }
      .hero {
        padding: 1rem 1rem 1.1rem;
      }
      .controls {
        grid-template-columns: 1.05fr 0.95fr;
        gap: 1.1rem 1.6rem;
      }
    }
    @media (max-width: 380px) {
      .topline {
        font-size: 0.56rem;
      }
      h1 {
        font-size: 1rem;
      }
      .hero-top {
        gap: 0.35rem;
        font-size: 0.54rem;
      }
      .controls {
        gap: 0.8rem 0.35rem;
      }
      .action-cluster {
        padding-right: 0.55rem;
      }
      .menu {
        gap: 0.75rem;
      }
    }
  </style>
</head>
<body>
  <main>
    <section class="dmg">
      <div class="topline">
        <span>Dot Matrix Handheld</span>
        <h1>ESP32 Boy</h1>
      </div>
      <section class="hero">
        <div class="hero-top">
          <span class="battery"></span>
          <span>Battery</span>
          <span class="hero-spacer">Dot Matrix With Stereo Sound</span>
        </div>
        <div class="screen-wrap">
          <canvas id="screen" width="160" height="144"></canvas>
        </div>
        <p class="brandline"><strong>Game Boy</strong> compatible web portal</p>
        <div class="status" id="status">connecting</div>
      </section>
      <section class="controls">
        <div class="dpad-shell">
          <div class="dpad">
            <button class="up" data-control="up" aria-label="Up">Up</button>
            <button class="left" data-control="left" aria-label="Left">Left</button>
            <div class="dpad-center" aria-hidden="true"></div>
            <button class="right" data-control="right" aria-label="Right">Right</button>
            <button class="down" data-control="down" aria-label="Down">Down</button>
          </div>
        </div>
        <div class="action-cluster">
          <button class="b" data-control="b" aria-label="B">B</button>
          <button class="a" data-control="a" aria-label="A">A</button>
        </div>
        <div class="menu">
          <button class="small" data-control="select" aria-label="Select">Select</button>
          <button class="small" data-control="start" aria-label="Start">Start</button>
        </div>
      </section>
      <div class="speaker" aria-hidden="true"></div>
      <section class="meta-panel">
        <div class="meta-head">
          <span>Link Status</span>
          <span>DMG-01</span>
        </div>
        <pre id="meta"></pre>
      </section>
    </section>
  </main>
  <script>
    const statusEl = document.getElementById('status');
    const metaEl = document.getElementById('meta');
    const canvas = document.getElementById('screen');
    const ctx = canvas.getContext('2d');
    const image = ctx.createImageData(160, 144);
    const palette = [
      [255,255,255],
      [132,130,132],
      [66,65,66],
      [0,0,0]
    ];
    const held = new Map();
    const releaseTimers = new Map();
    let audioContext;
    let audioScheduleTime = 0;
    let audioRequested = false;
    let socket;
    let latestState;
    let frames = 0;
    let lastFpsAt = performance.now();
    let fps = 0;

    function drawPackedFrame(buffer) {
      const bytes = new Uint8Array(buffer);
      if (bytes.length < 6 || bytes[0] !== 71 || bytes[1] !== 66 || bytes[2] !== 70) return;
      let src = 6;
      let pixel = 0;
      while (pixel < 160 * 144 && src < bytes.length) {
        const packed = bytes[src++];
        for (let shift = 0; shift < 8 && pixel < 160 * 144; shift += 2) {
          const rgb = palette[(packed >> shift) & 3];
          const dst = pixel++ * 4;
          image.data[dst] = rgb[0];
          image.data[dst + 1] = rgb[1];
          image.data[dst + 2] = rgb[2];
          image.data[dst + 3] = 255;
        }
      }
      ctx.putImageData(image, 0, 0);
      frames++;
      const now = performance.now();
      if (now - lastFpsAt >= 1000) {
        fps = frames;
        frames = 0;
        lastFpsAt = now;
        updateMeta();
      }
    }

    function ensureAudio() {
      if (latestState && latestState.audio && !latestState.audio.available) {
        return;
      }
      if (!window.AudioContext && !window.webkitAudioContext) {
        statusEl.textContent = 'audio unsupported';
        return;
      }
      if (!audioContext) {
        audioContext = new (window.AudioContext || window.webkitAudioContext)();
        audioScheduleTime = audioContext.currentTime + 0.08;
      }
      if (audioContext.state === 'suspended') {
        audioContext.resume().catch(() => {});
      }
      audioRequested = true;
      requestAudioStream(true);
    }

    function playAudioChunk(buffer) {
      const bytes = new Uint8Array(buffer);
      if (bytes.length < 9 || bytes[0] !== 71 || bytes[1] !== 66 || bytes[2] !== 65) return;
      if (!audioContext) return;

      const sampleRate = bytes[4] | (bytes[5] << 8);
      const sampleCount = bytes[6] | (bytes[7] << 8);
      if (!sampleRate || !sampleCount || bytes.length < 8 + sampleCount) return;

      const audioBuffer = audioContext.createBuffer(1, sampleCount, sampleRate);
      const channel = audioBuffer.getChannelData(0);
      for (let index = 0; index < sampleCount; index++) {
        channel[index] = (bytes[8 + index] - 128) / 128;
      }

      const source = audioContext.createBufferSource();
      source.buffer = audioBuffer;
      source.connect(audioContext.destination);

      const now = audioContext.currentTime;
      if (audioScheduleTime < now + 0.03 || audioScheduleTime > now + 0.5) {
        audioScheduleTime = now + 0.06;
      }
      source.start(audioScheduleTime);
      audioScheduleTime += sampleCount / sampleRate;
    }

    function updateMeta(state) {
      const parts = [`stream ${fps} fps`];
      if (state) {
        parts.push(`AP ${state.network.ssid}`);
        parts.push(`${state.network.ip}`);
        parts.push(`clients ${state.network.socketClients}`);
        parts.push(`heap ${state.memory.freeHeap}`);
        if (state.audio) {
          parts.push(state.audio.available
            ? `audio ${state.audio.enabled ? 'on' : 'off'} ${state.audio.sampleRate}hz`
            : 'audio disabled');
        }
        if (state.input) {
          parts.push(`web buttons ${state.input.webButtons}`);
          parts.push(`web directions ${state.input.webDirections}`);
          parts.push(`ff00 ${state.input.ff00}`);
        }
      }
      metaEl.textContent = parts.join('\n');
    }

    async function loadState() {
      const response = await fetch('/api/state');
      const state = await response.json();
      latestState = state;
      statusEl.textContent = `${state.network.ssid} ${state.network.ip}`;
      updateMeta(state);
      return state;
    }

    async function websocketPort() {
      if (latestState && latestState.network && latestState.network.websocketPort) {
        return latestState.network.websocketPort;
      }
      try {
        const state = await loadState();
        return state.network.websocketPort || 81;
      } catch (_) {
        return 81;
      }
    }

    async function sendInput(control, pressed) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ type: 'input', control, pressed }));
        return;
      }
      await fetch('/api/input', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `control=${encodeURIComponent(control)}&pressed=${pressed ? '1' : '0'}`
      }).catch(() => {});
    }

    function requestAudioStream(enabled) {
      if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({ type: 'audio', enabled }));
      }
    }

    function pressControl(button) {
      const control = button.dataset.control;
      if (!control) return;
      ensureAudio();
      if (releaseTimers.has(control)) {
        clearTimeout(releaseTimers.get(control));
        releaseTimers.delete(control);
      }
      button.dataset.held = 'true';
      if (held.has(control)) clearInterval(held.get(control));
      held.set(control, setInterval(() => sendInput(control, true), 250));
      sendInput(control, true);
      statusEl.textContent = `pressed ${control}`;
    }

    function releaseControl(button) {
      const control = button.dataset.control;
      if (!control) return;
      button.dataset.held = 'false';
      if (held.has(control)) {
        clearInterval(held.get(control));
        held.delete(control);
      }
      if (releaseTimers.has(control)) {
        clearTimeout(releaseTimers.get(control));
      }
      const timer = setTimeout(() => {
        sendInput(control, false);
        releaseTimers.delete(control);
      }, 120);
      releaseTimers.set(control, timer);
    }

    document.querySelectorAll('[data-control]').forEach((button) => {
      if (window.PointerEvent) {
        button.addEventListener('pointerdown', (event) => {
          event.preventDefault();
          try { button.setPointerCapture(event.pointerId); } catch (_) {}
          pressControl(button);
        });
        ['pointerup', 'pointercancel', 'lostpointercapture'].forEach((name) => {
          button.addEventListener(name, () => releaseControl(button));
        });
      } else {
        button.addEventListener('mousedown', (event) => {
          event.preventDefault();
          pressControl(button);
        });
        button.addEventListener('mouseup', () => releaseControl(button));
        button.addEventListener('mouseleave', () => releaseControl(button));
        button.addEventListener('touchstart', (event) => {
          event.preventDefault();
          pressControl(button);
        }, { passive: false });
        button.addEventListener('touchend', () => releaseControl(button));
        button.addEventListener('touchcancel', () => releaseControl(button));
      }
    });

    const keyMap = {
      ArrowUp: 'up',
      ArrowDown: 'down',
      ArrowLeft: 'left',
      ArrowRight: 'right',
      KeyZ: 'a',
      KeyX: 'b',
      Enter: 'start',
      ShiftLeft: 'select',
      ShiftRight: 'select'
    };
    const activeKeys = new Set();
    window.addEventListener('keydown', (event) => {
      const control = keyMap[event.code];
      if (!control || activeKeys.has(event.code)) return;
      ensureAudio();
      activeKeys.add(event.code);
      sendInput(control, true);
    });
    window.addEventListener('keyup', (event) => {
      const control = keyMap[event.code];
      if (!control) return;
      activeKeys.delete(event.code);
      sendInput(control, false);
    });

    async function connectSocket() {
      const port = await websocketPort();
      socket = new WebSocket(`ws://${location.hostname}:${port}/`);
      socket.binaryType = 'arraybuffer';
      socket.addEventListener('open', () => {
        statusEl.textContent = 'connected';
        if (audioRequested) requestAudioStream(true);
        loadState().catch(() => {});
      });
      socket.addEventListener('message', (event) => {
        if (typeof event.data === 'string') {
          try {
            const state = JSON.parse(event.data);
            updateMeta(state);
          } catch (_) {}
        } else {
          const bytes = new Uint8Array(event.data);
          if (bytes[0] === 71 && bytes[1] === 66 && bytes[2] === 65) {
            playAudioChunk(event.data);
          } else {
            drawPackedFrame(event.data);
          }
        }
      });
      socket.addEventListener('close', () => {
        statusEl.textContent = 'reconnecting';
        setTimeout(() => connectSocket().catch(() => {}), 1200);
      });
    }

    connectSocket().catch(() => {
      statusEl.textContent = 'reconnecting';
      setTimeout(() => connectSocket().catch(() => {}), 1200);
    });
    loadState().catch(() => {});
  </script>
</body>
</html>
)HTML";

WebPortalConfig config;
std::unique_ptr<WebServer> server;
std::unique_ptr<WebSocketsServer> websocket;
String ip_address = "0.0.0.0";
uint8_t socket_client_count = 0;
bool audio_stream_enabled = false;
uint32_t last_frame_ms = 0;
uint32_t frame_sequence = 0;
uint8_t packed_frame[6 + ((board::GAMEBOY_WIDTH * board::GAMEBOY_HEIGHT + 3) / 4)];
#if GB_ENABLE_AUDIO
uint8_t audio_frame[8 + 512];
#endif

String escape_json(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    switch (c) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped += c; break;
    }
  }
  return escaped;
}

String state_json() {
  String json;
  json.reserve(384);
  json += "{\"type\":\"state\",\"network\":{\"ssid\":\"";
  json += escape_json(config.ap_ssid ? String(config.ap_ssid) : String(""));
  json += "\",\"ip\":\"";
  json += escape_json(ip_address);
  json += "\",\"socketClients\":";
  json += String(socket_client_count);
  json += ",\"websocketPort\":";
  json += String(config.websocket_port);
  json += "},\"memory\":{\"freeHeap\":";
  json += String(ESP.getFreeHeap());
  json += ",\"minFreeHeap\":";
  json += String(ESP.getMinFreeHeap());
  json += "},\"stream\":{\"width\":";
  json += String(board::GAMEBOY_WIDTH);
  json += ",\"height\":";
  json += String(board::GAMEBOY_HEIGHT);
  json += ",\"intervalMs\":";
  json += String(config.stream_interval_ms);
  json += "},\"audio\":{\"available\":";
  json += board::WEB_AUDIO_ENABLED ? "true" : "false";
  json += ",\"enabled\":";
  json += (board::WEB_AUDIO_ENABLED && audio_stream_enabled) ? "true" : "false";
  json += ",\"sampleRate\":";
#if GB_ENABLE_AUDIO
  json += String(apu_sample_rate());
#else
  json += "0";
#endif
  json += "},\"input\":{\"buttons\":";
  json += String(touch_input_buttons());
  json += ",\"directions\":";
  json += String(touch_input_directions());
  json += ",\"webButtons\":";
  json += String(touch_input_web_buttons());
  json += ",\"webDirections\":";
  json += String(touch_input_web_directions());
  json += ",\"ff00\":";
  json += String(mem_get_joypad_register());
  json += "}}";
  return json;
}

bool extract_json_string_field(const String &json, const char *field_name,
                               String &value) {
  const String token = String("\"") + field_name + "\"";
  const int key_pos = json.indexOf(token);
  if (key_pos < 0) return false;
  const int colon_pos = json.indexOf(':', key_pos + token.length());
  if (colon_pos < 0) return false;
  const int quote_start = json.indexOf('"', colon_pos + 1);
  if (quote_start < 0) return false;

  String parsed;
  bool escaping = false;
  for (int index = quote_start + 1; index < json.length(); ++index) {
    const char c = json[index];
    if (escaping) {
      parsed += c;
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (c == '"') {
      value = parsed;
      return true;
    }
    parsed += c;
  }
  return false;
}

bool extract_json_bool_field(const String &json, const char *field_name,
                             bool &value) {
  const String token = String("\"") + field_name + "\"";
  const int key_pos = json.indexOf(token);
  if (key_pos < 0) return false;
  const int colon_pos = json.indexOf(':', key_pos + token.length());
  if (colon_pos < 0) return false;
  int value_pos = colon_pos + 1;
  while (value_pos < json.length() &&
         (json[value_pos] == ' ' || json[value_pos] == '\t' ||
          json[value_pos] == '\n' || json[value_pos] == '\r')) {
    value_pos++;
  }
  if (json.startsWith("true", value_pos) || json.startsWith("\"true\"", value_pos) ||
      json.startsWith("1", value_pos) || json.startsWith("\"1\"", value_pos)) {
    value = true;
    return true;
  }
  if (json.startsWith("false", value_pos) || json.startsWith("\"false\"", value_pos) ||
      json.startsWith("0", value_pos) || json.startsWith("\"0\"", value_pos)) {
    value = false;
    return true;
  }
  return false;
}

void publish_state() {
  if (websocket) {
    String json = state_json();
    websocket->broadcastTXT(json);
  }
}

void handle_input_command(const String &control, bool pressed, uint32_t now_ms) {
  if (!control.length()) return;
  touch_input_set_web_control(control.c_str(), pressed, now_ms);
  publish_state();
}

void handle_socket_event(uint8_t client_num, WStype_t type, uint8_t *payload,
                         size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      socket_client_count++;
      {
        String json = state_json();
        websocket->sendTXT(client_num, json);
      }
      publish_state();
      break;
    case WStype_DISCONNECTED:
      if (socket_client_count > 0) socket_client_count--;
      touch_input_clear_web_controls();
      if (socket_client_count == 0) audio_stream_enabled = false;
      publish_state();
      break;
    case WStype_TEXT: {
      String body;
      body.reserve(length);
      for (size_t index = 0; index < length; ++index) {
        body += static_cast<char>(payload[index]);
      }
      String type_value;
      if (!extract_json_string_field(body, "type", type_value)) break;
      if (type_value == "input") {
        String control;
        bool pressed = false;
        if (extract_json_string_field(body, "control", control) &&
            extract_json_bool_field(body, "pressed", pressed)) {
          handle_input_command(control, pressed, millis());
        }
      } else if (type_value == "audio") {
        bool enabled = false;
        if (extract_json_bool_field(body, "enabled", enabled)) {
          audio_stream_enabled = board::WEB_AUDIO_ENABLED && enabled;
          publish_state();
        }
      }
      break;
    }
    default:
      break;
  }
}

void configure_routes() {
  server->on("/", HTTP_GET, []() {
    server->sendHeader("Cache-Control", "no-store");
    server->send_P(200, PSTR("text/html; charset=utf-8"), kHtmlPage);
  });

  server->on("/api/state", HTTP_GET, []() {
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", state_json());
  });

  server->on("/api/input", HTTP_POST, []() {
    String control = server->arg("control");
    control.trim();
    const String pressed_arg = server->arg("pressed");
    const bool pressed = pressed_arg == "1" || pressed_arg == "true" ||
                         pressed_arg == "down";
    handle_input_command(control, pressed, millis());
    server->send(200, "application/json", state_json());
  });

  server->on("/api/input_state", HTTP_GET, []() {
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", state_json());
  });

  server->onNotFound([]() {
    server->send(404, "application/json", "{\"error\":\"not found\"}");
  });
}

void pack_frame(const uint8_t *framebuffer) {
  packed_frame[0] = 'G';
  packed_frame[1] = 'B';
  packed_frame[2] = 'F';
  packed_frame[3] = 1;
  packed_frame[4] = board::GAMEBOY_WIDTH;
  packed_frame[5] = board::GAMEBOY_HEIGHT;

  const size_t pixels = board::GAMEBOY_WIDTH * board::GAMEBOY_HEIGHT;
  size_t src = 0;
  size_t dst = 6;
  while (src < pixels) {
    uint8_t packed = 0;
    for (uint8_t shift = 0; shift < 8 && src < pixels; shift += 2) {
      packed |= (framebuffer[src++] & 0x03) << shift;
    }
    packed_frame[dst++] = packed;
  }
  frame_sequence++;
}

void maybe_stream_frame(uint32_t now_ms, const uint8_t *framebuffer) {
  if (!websocket || !framebuffer || socket_client_count == 0) return;
  if (now_ms - last_frame_ms < config.stream_interval_ms) return;
  last_frame_ms = now_ms;

  pack_frame(framebuffer);
  websocket->broadcastBIN(packed_frame, sizeof(packed_frame));
}

void maybe_stream_audio() {
#if GB_ENABLE_AUDIO
  if (!websocket || socket_client_count == 0 || !audio_stream_enabled) return;

  const size_t sample_count = apu_read_samples(audio_frame + 8, 512);
  if (!sample_count) return;

  const uint16_t sample_rate = apu_sample_rate();
  audio_frame[0] = 'G';
  audio_frame[1] = 'B';
  audio_frame[2] = 'A';
  audio_frame[3] = 1;
  audio_frame[4] = sample_rate & 0xFF;
  audio_frame[5] = sample_rate >> 8;
  audio_frame[6] = sample_count & 0xFF;
  audio_frame[7] = sample_count >> 8;
  websocket->broadcastBIN(audio_frame, 8 + sample_count);
#endif
}

}  // namespace

bool web_portal_begin(const WebPortalConfig &portal_config) {
  config = portal_config;
  server.reset(new WebServer(config.http_port));
  websocket.reset(new WebSocketsServer(config.websocket_port));

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(config.ap_ssid, config.ap_password)) {
    ip_address = "0.0.0.0";
    return false;
  }

  ip_address = WiFi.softAPIP().toString();
  configure_routes();
  server->begin();
  websocket->begin();
  websocket->onEvent([](uint8_t client_num, WStype_t type, uint8_t *payload,
                        size_t length) {
    handle_socket_event(client_num, type, payload, length);
  });
  return true;
}

void web_portal_loop(uint32_t now_ms, const uint8_t *framebuffer) {
  if (server) server->handleClient();
  if (websocket) websocket->loop();
  touch_input_maintain(now_ms);
  maybe_stream_frame(now_ms, framebuffer);
  maybe_stream_audio();
}

const char *web_portal_ip(void) { return ip_address.c_str(); }

uint8_t web_portal_client_count(void) { return socket_client_count; }
