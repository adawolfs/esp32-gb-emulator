const palette = [
  [255, 255, 255],
  [132, 130, 132],
  [66, 65, 66],
  [0, 0, 0]
];

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

const canvas = document.getElementById('screen');
const statusNode = document.getElementById('status');
const metaNode = document.getElementById('meta');
const controlButtons = Array.from(document.querySelectorAll('[data-control]'));

let imageData = null;
let socket = null;
let latestState = null;
let reconnectTimer = 0;
let audioContext = null;
let audioScheduleTime = 0;
let audioRequested = false;
let frames = 0;
let lastFpsAt = performance.now();
let fps = 0;

const heldIntervals = new Map();
const releaseTimers = new Map();
const activeKeys = new Set();

function setStatus(text) {
  statusNode.textContent = text;
}

function setSocketConnected(connected) {
  document.body.dataset.socket = connected ? 'connected' : 'disconnected';
}

function setHeld(control, isHeld) {
  const button = document.querySelector(`[data-control="${control}"]`);
  if (!button) return;
  button.dataset.held = isHeld ? 'true' : 'false';
}

function updateMeta(state = latestState) {
  const parts = [`stream ${fps} fps`];
  if (state) {
    parts.push(`AP ${state.network.ssid}`);
    parts.push(`${state.network.ip}`);
    parts.push(`clients ${state.network.socketClients}`);
    parts.push(`heap ${state.memory.freeHeap}`);
    if (state.audio) {
      parts.push(
        state.audio.available
          ? `audio ${state.audio.enabled ? 'on' : 'off'} ${state.audio.sampleRate}hz`
          : 'audio disabled'
      );
    }
    if (state.input) {
      parts.push(`web buttons ${state.input.webButtons}`);
      parts.push(`web directions ${state.input.webDirections}`);
      parts.push(`ff00 ${state.input.ff00}`);
    }
  }
  metaNode.textContent = parts.join('\n');
}

function drawPackedFrame(buffer) {
  if (!canvas) return;

  const context = canvas.getContext('2d');
  if (!context) return;

  if (!imageData) {
    imageData = context.createImageData(160, 144);
  }

  const bytes = new Uint8Array(buffer);
  if (bytes.length < 6 || bytes[0] !== 71 || bytes[1] !== 66 || bytes[2] !== 70) {
    return;
  }

  let src = 6;
  let pixel = 0;
  while (pixel < 160 * 144 && src < bytes.length) {
    const packed = bytes[src++];
    for (let shift = 0; shift < 8 && pixel < 160 * 144; shift += 2) {
      const rgb = palette[(packed >> shift) & 0x03];
      const dst = pixel++ * 4;
      imageData.data[dst] = rgb[0];
      imageData.data[dst + 1] = rgb[1];
      imageData.data[dst + 2] = rgb[2];
      imageData.data[dst + 3] = 255;
    }
  }

  context.putImageData(imageData, 0, 0);
  frames += 1;
  const now = performance.now();
  if (now - lastFpsAt >= 1000) {
    fps = frames;
    frames = 0;
    lastFpsAt = now;
    updateMeta();
  }
}

function requestAudioStream(enabled) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({ type: 'audio', enabled }));
  }
}

function ensureAudio() {
  if (latestState?.audio && !latestState.audio.available) {
    return;
  }
  if (!window.AudioContext && !window.webkitAudioContext) {
    setStatus('audio unsupported');
    return;
  }
  if (!audioContext) {
    const Context = window.AudioContext || window.webkitAudioContext;
    audioContext = new Context();
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
  if (bytes.length < 9 || bytes[0] !== 71 || bytes[1] !== 66 || bytes[2] !== 65) {
    return;
  }

  if (!audioContext) return;

  const sampleRate = bytes[4] | (bytes[5] << 8);
  const sampleCount = bytes[6] | (bytes[7] << 8);
  if (!sampleRate || !sampleCount || bytes.length < 8 + sampleCount) return;

  const audioBuffer = audioContext.createBuffer(1, sampleCount, sampleRate);
  const channel = audioBuffer.getChannelData(0);
  for (let index = 0; index < sampleCount; index += 1) {
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

async function loadState() {
  const response = await fetch('/api/state');
  const state = await response.json();
  latestState = state;
  setStatus(`${state.network.ssid} ${state.network.ip}`);
  updateMeta(state);
  return state;
}

async function websocketPort() {
  if (latestState?.network?.websocketPort) {
    return latestState.network.websocketPort;
  }
  try {
    const state = await loadState();
    return state.network.websocketPort || 81;
  } catch {
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

function pressControl(control) {
  ensureAudio();

  if (releaseTimers.has(control)) {
    clearTimeout(releaseTimers.get(control));
    releaseTimers.delete(control);
  }

  setHeld(control, true);
  if (heldIntervals.has(control)) {
    clearInterval(heldIntervals.get(control));
  }

  heldIntervals.set(control, setInterval(() => sendInput(control, true), 250));
  sendInput(control, true);
  setStatus(`pressed ${control}`);
}

function releaseControl(control) {
  setHeld(control, false);

  if (heldIntervals.has(control)) {
    clearInterval(heldIntervals.get(control));
    heldIntervals.delete(control);
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

function bindPointerHandlers(button) {
  const control = button.dataset.control;
  button.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    try {
      button.setPointerCapture(event.pointerId);
    } catch {
      // Ignore capture failures.
    }
    pressControl(control);
  });

  ['pointerup', 'pointercancel', 'lostpointercapture'].forEach((name) => {
    button.addEventListener(name, () => releaseControl(control));
  });
}

async function connectSocket() {
  setSocketConnected(false);
  const port = await websocketPort();
  const nextSocket = new WebSocket(`ws://${window.location.hostname}:${port}/`);
  nextSocket.binaryType = 'arraybuffer';
  socket = nextSocket;

  nextSocket.addEventListener('open', () => {
    setSocketConnected(true);
    setStatus('connected', true);
    if (audioRequested) requestAudioStream(true);
    loadState().catch(() => {});
  });

  nextSocket.addEventListener('message', (event) => {
    if (typeof event.data === 'string') {
      try {
        const state = JSON.parse(event.data);
        latestState = state;
        updateMeta(state);
      } catch {
        // Ignore malformed messages.
      }
      return;
    }

    const bytes = new Uint8Array(event.data);
    if (bytes[0] === 71 && bytes[1] === 66 && bytes[2] === 65) {
      playAudioChunk(event.data);
    } else {
      drawPackedFrame(event.data);
    }
  });

  nextSocket.addEventListener('close', () => {
    setSocketConnected(false);
    setStatus('reconnecting');
    if (socket === nextSocket) {
      socket = null;
    }
    window.clearTimeout(reconnectTimer);
    reconnectTimer = window.setTimeout(() => {
      connectSocket().catch(() => {});
    }, 1200);
  });

  nextSocket.addEventListener('error', () => {
    setSocketConnected(false);
  });
}

function onKeyDown(event) {
  const control = keyMap[event.code];
  if (!control || activeKeys.has(event.code)) return;
  ensureAudio();
  activeKeys.add(event.code);
  sendInput(control, true);
}

function onKeyUp(event) {
  const control = keyMap[event.code];
  if (!control) return;
  activeKeys.delete(event.code);
  sendInput(control, false);
}

controlButtons.forEach((button) => bindPointerHandlers(button));

window.addEventListener('keydown', onKeyDown);
window.addEventListener('keyup', onKeyUp);

connectSocket().catch(() => {
  setSocketConnected(false);
  setStatus('reconnecting');
  reconnectTimer = window.setTimeout(() => {
    connectSocket().catch(() => {});
  }, 1200);
});

loadState().catch(() => {});

window.addEventListener('pagehide', () => {
  window.clearTimeout(reconnectTimer);
  if (socket) socket.close();
  heldIntervals.forEach((timer) => clearInterval(timer));
  heldIntervals.clear();
  releaseTimers.forEach((timer) => clearTimeout(timer));
  releaseTimers.clear();
});

if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker.register('/sw.js').catch(() => {});
  });
}
