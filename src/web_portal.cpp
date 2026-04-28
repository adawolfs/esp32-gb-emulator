#include "web_portal.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
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
bool spiffs_mounted = false;
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

const char *content_type_for_path(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg")) return "image/svg+xml";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".woff2")) return "font/woff2";
  if (path.endsWith(".txt")) return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

bool request_targets_static_asset(const String &path) {
  const int slash_pos = path.lastIndexOf('/');
  const int dot_pos = path.lastIndexOf('.');
  return dot_pos > slash_pos;
}

bool stream_spiffs_file(const String &raw_path) {
  if (!spiffs_mounted) return false;

  String path = raw_path;
  if (!path.length()) path = "/";
  if (!path.startsWith("/")) path = "/" + path;
  if (path.endsWith("/")) path += "index.html";

  if (!SPIFFS.exists(path)) return false;

  File file = SPIFFS.open(path, FILE_READ);
  if (!file) return false;

  if (path.endsWith(".html")) {
    server->sendHeader("Cache-Control", "no-store");
  } else {
    server->sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  }

  server->streamFile(file, content_type_for_path(path));
  file.close();
  return true;
}

void send_ui_unavailable() {
  server->send(503, "text/plain; charset=utf-8",
               "Web UI not found in SPIFFS. Build the Vite app and upload the filesystem image.");
}

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
    if (!stream_spiffs_file("/index.html")) {
      send_ui_unavailable();
    }
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
    const String uri = server->uri();
    if (uri.startsWith("/api/")) {
      server->send(404, "application/json", "{\"error\":\"not found\"}");
      return;
    }

    if (stream_spiffs_file(uri)) return;
    if (!request_targets_static_asset(uri) && stream_spiffs_file("/index.html")) {
      return;
    }

    if (spiffs_mounted) {
      server->send(404, "text/plain; charset=utf-8", "static file not found");
    } else {
      send_ui_unavailable();
    }
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
  spiffs_mounted = SPIFFS.begin(false);
  if (!spiffs_mounted) {
    Serial.println("SPIFFS mount failed; static UI unavailable until filesystem is uploaded.");
  }

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
