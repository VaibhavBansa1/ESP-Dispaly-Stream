/*
 * TFT Stream Portal  -  ESP32-S3 + ST7789 (2.4" 240x320 V1.3 board)
 *
 * Browser se video, image, camera feed aur drawing seedhe TFT pe stream karta hai.
 * Screen ka size code khud detect karta hai, toh koi bhi rotation chalega.
 *
 * Libraries (Library Manager se):
 *   - Adafruit GFX Library
 *   - Adafruit ST7735 and ST7789 Library
 *   - WebSockets  (by Markus Sattler)
 *
 * Wiring (ESP32-S3):
 *   CS -> 10,  DC -> 9,  RST -> 13,  SCK -> 12,  SDA -> 11
 *   LED -> 3.3V,  VCC -> 3.3V,  GND -> GND
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ---------------- WIFI ----------------
const char* ssid     = "YOUR WIFI NAME";   // <-- apna WiFi naam
const char* password = "WiFi Password";          // <-- apna WiFi password

// ---------------- DISPLAY ----------------
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST  13
#define TFT_SCLK 12
#define TFT_MOSI 11

#define SPI_HZ 20000000

// ===== agar kuch galat lage to sirf ye teen badlo =====

// 1 aur 3 dono landscape hain. Ulta lage to doosra try karo.
#define TFT_ROTATION 3

// Sab kuch ulta (kaala safed, safed kaala) -> false kar do
#define INVERT_DISPLAY false

// Red aur blue badle hue lagein -> 1 kar do
#define SWAP_RED_BLUE 0

// Boot pe test pattern dikhaye. 0 = band.
#define SHOW_TEST_PATTERN 1

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);
WebSocketsServer webSocket(81);

// setup() mein bhare jaate hain
int scrW = 0, scrH = 0;      // asli screen size
int srcW = 0, srcH = 0;      // stream frame = screen ka aadha
int bandH = 0;               // ek band = srcH / 4 rows
int bandBytes = 0;           // srcW * bandH * 2

// Do rows ka scratch buffer (max 320 wide)
uint16_t lineBuf[320 * 2];

// ---------------- WEB PORTAL ----------------
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>TFT Stream Portal</title>
  <style>
    body { font-family:'Courier New',monospace; background:#0d0d0d; color:#00ff00;
           text-align:center; margin:0; padding:10px; }
    h2 { border-bottom:2px solid #00ff00; padding-bottom:10px; }
    button { background:#1a1a1a; border:1px solid #00ff00; color:#00ff00; padding:12px 20px;
             font-size:14px; margin:5px; border-radius:4px; cursor:pointer;
             transition:.2s; font-weight:bold; }
    button:hover { background:#00ff00; color:#000; }
    button:focus-visible { outline:2px solid #fff; outline-offset:2px; }
    .panel { border:1px dashed #00ff00; padding:15px; margin:15px auto; width:90%;
             max-width:560px; border-radius:8px; background:#111; }
    input[type=file] { display:none; }
    .file-label { background:#222; border:1px solid #00ff00; padding:8px 15px;
                  border-radius:4px; cursor:pointer; display:inline-block; margin-top:10px; }
    input[type=range] { width:90%; padding:8px; margin:10px 0; background:#222;
                        color:#00ff00; border:1px solid #00ff00; border-radius:4px; }
    #previewCanvas, #drawCanvas {
      display:block; margin:10px auto; border:1px solid #00ff00; background:#000;
      image-rendering:pixelated; cursor:crosshair; touch-action:none; max-width:100%;
    }
    #drawCanvas { display:none; }
    .tab-btn { width:40%; font-size:12px; padding:8px; }
    .active-tab { background:#00ff00; color:#000; }
  </style>
</head>
<body>
  <h2>TFT Stream Portal</h2>

  <div class="panel">
    <div>
      <button id="tabVideo" class="tab-btn active-tab" onclick="switchTab('video')">Camera / Media</button>
      <button id="tabDraw" class="tab-btn" onclick="switchTab('draw')">Draw</button>
    </div>

    <div id="videoSection">
      <button onclick="startCamera()">Start webcam</button>
      <label class="file-label">
        <input type="file" id="mediaInput" accept="video/*,image/*"> Upload video or image
      </label>

      <video id="sourceVideo" autoplay loop muted playsinline style="display:none;"></video>
      <img id="sourceImage" style="display:none;">

      <br><label>Rotation: <span id="rotVal">0</span> deg</label>
      <input type="range" id="rotation" min="0" max="360" value="0">

      <br><label>Zoom: <span id="zoomVal">1.0</span>x</label>
      <input type="range" id="zoom" min="0.1" max="5.0" step="0.1" value="1.0">

      <br><label>Brightness: <span id="brightVal">1.0</span>x</label>
      <input type="range" id="brightness" min="0.2" max="2.5" step="0.1" value="1.0">

      <label style="display:inline-block;margin-top:10px;">
        <input type="checkbox" id="fillScreen" checked> Fill the whole screen (crop edges)
      </label>
      <label style="display:inline-block;margin-top:10px;">
        <input type="checkbox" id="grayscale"> Grayscale
      </label>
    </div>

    <div id="drawSection" style="display:none;">
      <button onclick="clearCanvas()">Clear board</button>
      <input type="color" id="penColor" value="#00ff00">
      <canvas id="drawCanvas"></canvas>
      <p style="font-size:11px;margin:0;color:#aaa;">Pen uthate hi stroke TFT pe chala jaata hai.</p>
    </div>

    <h4 id="dimLabel">Connecting...</h4>
    <canvas id="previewCanvas"></canvas>

    <button id="streamBtn" onclick="toggleStream()" style="width:100%;font-size:18px;padding:15px;margin-top:10px;">
      Start live stream
    </button>
    <button onclick="sendBands()">Send one frame</button>
    <button onclick="fetch('/clear')">Blank the TFT</button>
    <button onclick="fetch('/test')">Test pattern</button>
    <p id="status" style="margin-top:10px;"></p>
  </div>

<script>
// Ye values ESP32 se aati hain
let SRC_W = 160, SRC_H = 120, BAND_H = 30;
const BANDS = 4;
let bandBuf = null;

const video   = document.getElementById('sourceVideo');
const image   = document.getElementById('sourceImage');
const preview = document.getElementById('previewCanvas');
const pctx    = preview.getContext('2d', { willReadFrequently: true });
const drawCv  = document.getElementById('drawCanvas');
const dctx    = drawCv.getContext('2d', { willReadFrequently: true });

let isStreaming = false, currentTab = 'video', isDrawing = false, mediaType = 'none';
let ws = null;

function setStatus(t) { document.getElementById('status').innerText = t; }

fetch('/dims').then(r => r.json()).then(d => {
  SRC_W = d.srcW; SRC_H = d.srcH; BAND_H = d.bandH;
  bandBuf = new Uint8Array(SRC_W * BAND_H * 2 + 1);

  [preview, drawCv].forEach(c => {
    c.width = SRC_W; c.height = SRC_H;
    c.style.width = (SRC_W * 2) + 'px';
    c.style.height = (SRC_H * 2) + 'px';
  });

  dctx.fillStyle = '#000'; dctx.fillRect(0, 0, SRC_W, SRC_H);
  dctx.lineWidth = 3; dctx.lineCap = 'round'; dctx.lineJoin = 'round';

  document.getElementById('dimLabel').innerText =
    'TFT preview (' + SRC_W + 'x' + SRC_H + ' -> ' + d.scrW + 'x' + d.scrH + ')';

  ws = new WebSocket('ws://' + window.location.hostname + ':81/');
  ws.binaryType = 'arraybuffer';
  ws.onopen  = () => setStatus('WebSocket connected. Link stable.');
  ws.onclose = () => setStatus('WebSocket disconnected. Refresh the page.');
}).catch(() => setStatus('Could not read screen size from the board.'));

function switchTab(tab) {
  currentTab = tab;
  document.getElementById('videoSection').style.display = tab === 'video' ? 'block' : 'none';
  document.getElementById('drawSection').style.display  = tab === 'draw'  ? 'block' : 'none';
  document.getElementById('tabVideo').className = tab === 'video' ? 'tab-btn active-tab' : 'tab-btn';
  document.getElementById('tabDraw').className  = tab === 'draw'  ? 'tab-btn active-tab' : 'tab-btn';
  preview.style.display = tab === 'video' ? 'block' : 'none';
  drawCv.style.display  = tab === 'draw'  ? 'block' : 'none';
  document.getElementById('streamBtn').style.display = tab === 'video' ? 'block' : 'none';
}

// ---------- drawing ----------
function posOf(canvas, evt) {
  const r = canvas.getBoundingClientRect();
  return { x: (evt.clientX - r.left) / (r.width / SRC_W),
           y: (evt.clientY - r.top)  / (r.height / SRC_H) };
}
function startDrawing(e) {
  isDrawing = true;
  dctx.strokeStyle = document.getElementById('penColor').value;
  const p = posOf(drawCv, e);
  dctx.beginPath(); dctx.moveTo(p.x, p.y);
}
function draw(e) {
  if (!isDrawing) return;
  const p = posOf(drawCv, e);
  dctx.lineTo(p.x, p.y); dctx.stroke();
}
function stopDrawing() {
  if (isDrawing) sendBands();
  isDrawing = false;
}
drawCv.addEventListener('mousedown', startDrawing);
drawCv.addEventListener('mousemove', draw);
drawCv.addEventListener('mouseup', stopDrawing);
drawCv.addEventListener('mouseleave', stopDrawing);
drawCv.addEventListener('touchstart', e => { e.preventDefault(); startDrawing(e.touches[0]); }, {passive:false});
drawCv.addEventListener('touchmove',  e => { e.preventDefault(); draw(e.touches[0]); }, {passive:false});
drawCv.addEventListener('touchend',   e => { e.preventDefault(); stopDrawing(); }, {passive:false});

function clearCanvas() {
  dctx.fillStyle = '#000'; dctx.fillRect(0, 0, SRC_W, SRC_H);
  sendBands();
}

// ---------- media input ----------
async function startCamera() {
  try {
    if (!navigator.mediaDevices) throw new Error('no camera api');
    const stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment' } });
    mediaType = 'video';
    video.srcObject = stream; video.play();
    setStatus('Camera active.');
  } catch (err) {
    setStatus('Camera needs an HTTPS link. Upload an image or video instead.');
  }
}

document.getElementById('mediaInput').addEventListener('change', function (e) {
  if (!e.target.files.length) return;
  const file = e.target.files[0];
  const url  = URL.createObjectURL(file);
  if (file.type.startsWith('video')) {
    mediaType = 'video'; video.srcObject = null; video.src = url; video.play();
  } else {
    mediaType = 'image'; image.src = url;
  }
  setStatus('Loaded ' + file.name);
});

['rotation','zoom','brightness'].forEach(id => {
  document.getElementById(id).addEventListener('input', e => {
    const map = { rotation:'rotVal', zoom:'zoomVal', brightness:'brightVal' };
    document.getElementById(map[id]).innerText = e.target.value;
  });
});

// ---------- frame processing ----------
function drawSource() {
  pctx.fillStyle = '#000';
  pctx.fillRect(0, 0, SRC_W, SRC_H);
  const src = mediaType === 'video' ? video : (mediaType === 'image' ? image : null);
  if (!src) return;
  const sw = src.videoWidth || src.naturalWidth;
  const sh = src.videoHeight || src.naturalHeight;
  if (!sw || !sh) return;

  const rot  = +document.getElementById('rotation').value * Math.PI / 180;
  const zoom = +document.getElementById('zoom').value;
  const fill = document.getElementById('fillScreen').checked;

  const fit  = fill ? Math.max(SRC_W / sw, SRC_H / sh)
                    : Math.min(SRC_W / sw, SRC_H / sh);
  const s = fit * zoom;

  pctx.save();
  pctx.translate(SRC_W / 2, SRC_H / 2);
  pctx.rotate(rot);
  pctx.scale(s, s);
  pctx.drawImage(src, -sw / 2, -sh / 2);
  pctx.restore();
}

function rgb565(r, g, b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

function sendBands() {
  if (!ws || ws.readyState !== WebSocket.OPEN) { setStatus('WebSocket not connected.'); return; }
  const ctx = currentTab === 'draw' ? dctx : pctx;
  if (currentTab === 'video') drawSource();

  const bright = +document.getElementById('brightness').value;
  const gray   = document.getElementById('grayscale').checked;
  const img    = ctx.getImageData(0, 0, SRC_W, SRC_H).data;

  for (let band = 0; band < BANDS; band++) {
    bandBuf[0] = band;
    let o = 1;
    const start = band * BAND_H * SRC_W * 4;
    for (let i = 0; i < BAND_H * SRC_W; i++) {
      let r = img[start + i * 4], g = img[start + i * 4 + 1], b = img[start + i * 4 + 2];
      if (gray) { const l = (r * 0.299 + g * 0.587 + b * 0.114); r = g = b = l; }
      r = Math.min(255, r * bright); g = Math.min(255, g * bright); b = Math.min(255, b * bright);
      const c = rgb565(r | 0, g | 0, b | 0);
      bandBuf[o++] = c >> 8;
      bandBuf[o++] = c & 0xFF;
    }
    ws.send(bandBuf);
  }
}

function streamLoop() {
  if (!isStreaming) return;
  sendBands();
  setStatus('Streaming at ' + new Date().toLocaleTimeString());
  setTimeout(streamLoop, 80);
}

function toggleStream() {
  isStreaming = !isStreaming;
  const btn = document.getElementById('streamBtn');
  if (isStreaming) {
    btn.innerText = 'Stop live stream';
    btn.style.background = '#ff0000'; btn.style.color = '#fff';
    streamLoop();
  } else {
    btn.innerText = 'Start live stream';
    btn.style.background = '#1a1a1a'; btn.style.color = '#00ff00';
    setStatus('Stream paused.');
  }
}

setInterval(() => { if (!isStreaming && currentTab === 'video') drawSource(); }, 60);
</script>
</body>
</html>
)rawliteral";

// ---------------- COLOUR + TEST ----------------
static inline uint16_t fixColor(uint16_t c) {
#if SWAP_RED_BLUE
  return ((c & 0x001F) << 11) | (c & 0x07E0) | ((c & 0xF800) >> 11);
#else
  return c;
#endif
}

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return fixColor(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void testPattern() {
  tft.fillScreen(ST77XX_BLACK);

  tft.fillRect(0, 0, 30, 30, rgb(255, 0, 0));                    // LAAL
  tft.fillRect(scrW - 30, 0, 30, 30, rgb(0, 255, 0));            // HARA
  tft.fillRect(0, scrH - 30, 30, 30, rgb(0, 0, 255));            // NEELA
  tft.fillRect(scrW - 30, scrH - 30, 30, 30, rgb(255, 255, 0));  // PEELA

  tft.drawRect(0, 0, scrW, scrH, rgb(255, 255, 255));

  int barH = (scrH - 80) / 6;
  const uint8_t bars[6][3] = { {255,0,0}, {0,255,0}, {0,0,255},
                               {255,255,0}, {0,255,255}, {255,0,255} };
  for (int i = 0; i < 6; i++) {
    tft.fillRect(40, 40 + i * barH, scrW - 80, barH - 3,
                 rgb(bars[i][0], bars[i][1], bars[i][2]));
  }

  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(45, scrH - 30);
  tft.print(scrW); tft.print(" x "); tft.print(scrH);
}

// ---------------- STREAM RENDERING ----------------
// Ek source row -> do screen rows (2x upscale)
void renderBand(uint8_t band, uint8_t *data) {
  for (int row = 0; row < bandH; row++) {
    uint8_t *src = data + row * srcW * 2;
    for (int x = 0; x < srcW; x++) {
      uint16_t c = fixColor(((uint16_t)src[x * 2] << 8) | src[x * 2 + 1]);
      lineBuf[x * 2]            = c;
      lineBuf[x * 2 + 1]        = c;
      lineBuf[scrW + x * 2]     = c;
      lineBuf[scrW + x * 2 + 1] = c;
    }
    tft.drawRGBBitmap(0, band * bandH * 2 + row * 2, lineBuf, scrW, 2);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_BIN && length == (size_t)(bandBytes + 1)) {
    uint8_t band = payload[0];
    if (band >= 4) return;
    renderBand(band, payload + 1);
  }
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  // MISO -1 rakha hai taaki GPIO13 reset ke liye free rahe
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);

  tft.init(240, 320);              // panel native size
  tft.setSPISpeed(SPI_HZ);
  tft.setRotation(TFT_ROTATION);
  tft.invertDisplay(INVERT_DISPLAY);

  // Screen ka asli size yahin se aata hai - kahin hardcode nahi
  scrW  = tft.width();
  scrH  = tft.height();
  srcW  = scrW / 2;
  srcH  = scrH / 2;
  bandH = srcH / 4;
  bandBytes = srcW * bandH * 2;

  Serial.printf("Screen %dx%d | stream %dx%d | band %d rows, %d bytes\n",
                scrW, scrH, srcW, srcH, bandH, bandBytes);

  tft.fillScreen(ST77XX_BLACK);

#if SHOW_TEST_PATTERN
  testPattern();
  delay(5000);
  tft.fillScreen(ST77XX_BLACK);
#endif

  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 40);
  tft.println("Connecting WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("gadget")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS ready at http://gadget.local");
  }

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  server.on("/", []() { server.send(200, "text/html", htmlPage); });

  server.on("/dims", []() {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"scrW\":%d,\"scrH\":%d,\"srcW\":%d,\"srcH\":%d,\"bandH\":%d}",
             scrW, scrH, srcW, srcH, bandH);
    server.send(200, "application/json", buf);
  });

  server.on("/clear", []() {
    tft.fillScreen(ST77XX_BLACK);
    server.send(200, "text/plain", "ok");
  });

  server.on("/test", []() {
    testPattern();
    server.send(200, "text/plain", "ok");
  });

  server.begin();

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 30);
  tft.println("Ready");
  tft.setCursor(10, 70);
  tft.println("gadget.local");
  tft.setCursor(10, 110);
  tft.println(WiFi.localIP());
}

void loop() {
  webSocket.loop();
  server.handleClient();
}
