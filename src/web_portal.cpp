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
      color-scheme: dark;
      font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
      background: #111510;
      color: #edf5df;
    }
    * { box-sizing: border-box; touch-action: none; }
    body { margin: 0; min-height: 100vh; background: radial-gradient(circle at 50% 0%, #263222, #111510 62%); }
    main { width: min(920px, 100%); margin: 0 auto; padding: 18px 14px 28px; }
    header { display: flex; justify-content: space-between; align-items: baseline; gap: 12px; margin-bottom: 12px; }
    h1 { margin: 0; font-size: 20px; font-weight: 700; letter-spacing: 0; }
    .status { color: #aebd9b; font-size: 12px; text-align: right; }
    .stage { display: grid; grid-template-columns: minmax(240px, 1fr) minmax(260px, 0.9fr); gap: 16px; align-items: center; }
    .screen-wrap { display: grid; place-items: center; padding: 12px; border: 1px solid #3e4f36; background: #0b0d0a; }
    canvas { width: min(100%, 480px); image-rendering: pixelated; aspect-ratio: 160 / 144; background: #000; border: 2px solid #6c805c; }
    .controls { display: grid; grid-template-columns: 1fr 1fr; gap: 18px; align-items: center; }
    .dpad { display: grid; grid-template-columns: repeat(3, 74px); grid-template-rows: repeat(3, 58px); gap: 6px; justify-content: center; }
    .actions { display: grid; grid-template-columns: repeat(2, 82px); gap: 14px; justify-content: center; align-items: center; }
    .menu { grid-column: 1 / -1; display: flex; justify-content: center; gap: 10px; margin-top: 8px; }
    button {
      border: 1px solid #8da179;
      border-radius: 8px;
      background: #1b2418;
      color: #edf5df;
      font: inherit;
      min-height: 48px;
      user-select: none;
      -webkit-user-select: none;
    }
    button[data-held="true"] { background: #c7ef6d; color: #111510; border-color: #e3ff9a; }
    .a, .b { min-height: 82px; border-radius: 50%; font-size: 20px; }
    .up { grid-column: 2; grid-row: 1; }
    .left { grid-column: 1; grid-row: 2; }
    .right { grid-column: 3; grid-row: 2; }
    .down { grid-column: 2; grid-row: 3; }
    .small { width: 96px; min-height: 38px; font-size: 12px; }
    pre { white-space: pre-wrap; margin: 12px 0 0; color: #aebd9b; font-size: 11px; }
    @media (max-width: 720px) {
      .stage { grid-template-columns: 1fr; }
      .controls { gap: 10px; }
      .dpad { grid-template-columns: repeat(3, 62px); grid-template-rows: repeat(3, 52px); }
      .actions { grid-template-columns: repeat(2, 74px); }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>ESP32 Game Boy</h1>
      <div class="status" id="status">connecting</div>
    </header>
    <section class="stage">
      <div class="screen-wrap">
        <canvas id="screen" width="160" height="144"></canvas>
      </div>
      <div>
        <div class="controls">
          <div class="dpad">
            <button class="up" data-control="up">UP</button>
            <button class="left" data-control="left">LEFT</button>
            <button class="right" data-control="right">RIGHT</button>
            <button class="down" data-control="down">DOWN</button>
          </div>
          <div class="actions">
            <button class="b" data-control="b">B</button>
            <button class="a" data-control="a">A</button>
          </div>
          <div class="menu">
            <button class="small" data-control="select">SELECT</button>
            <button class="small" data-control="start">START</button>
          </div>
        </div>
        <pre id="meta"></pre>
      </div>
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
