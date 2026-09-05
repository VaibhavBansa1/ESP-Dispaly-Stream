#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);
WebSocketsServer webSocket(81); // WebSocket Stream running on port 81

int currentMode = 0; 

// Variables for custom image/video processing
uint8_t custom_bmp[1024] = {0}; // 128x64 pixels = 1024 bytes
bool customAnimationEnabled = false;

// Animation State Variables (Non-blocking)
unsigned long lastFrameTime = 0;
int frameDelay = 50;

int offset = 0;
int sonarRadius = 0;
int drops[21];
int snowX[30], snowY[30];
int boxX = 10, boxY = 10, dirX = 2, dirY = 2;
int steinsStep = 0;

// Forward declaration
void drawAnimationFrame();

// --- THE WEB PORTAL HTML & JAVASCRIPT ---
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Future Gadget Lab OLED</title>
  <style>
    body { font-family: 'Courier New', Courier, monospace; background-color: #0d0d0d; color: #00ff00; text-align: center; margin: 0; padding: 10px; }
    h2 { border-bottom: 2px solid #00ff00; padding-bottom: 10px; }
    button { background-color: #1a1a1a; border: 1px solid #00ff00; color: #00ff00; padding: 12px 20px; font-size: 14px; margin: 5px; border-radius: 4px; cursor: pointer; transition: 0.2s; font-weight: bold;}
    button:hover { background-color: #00ff00; color: #000; }
    
    .panel { border: 1px dashed #00ff00; padding: 15px; margin: 15px auto; width: 90%; max-width: 500px; border-radius: 8px; background-color: #111;}
    input[type=file] { display: none; }
    .file-label { background-color: #222; border: 1px solid #00ff00; padding: 8px 15px; border-radius: 4px; cursor: pointer; display: inline-block; margin-top: 10px;}
    
    select, input[type=range] { width: 90%; padding: 8px; margin: 10px 0; background: #222; color: #00ff00; border: 1px solid #00ff00; border-radius: 4px; }
    
    #previewCanvas, #drawCanvas { 
      display: block; margin: 10px auto; border: 1px solid #00ff00; background-color: black;
      image-rendering: pixelated; cursor: crosshair; touch-action: none;
    }
    #previewCanvas { width: 256px; height: 128px; } 
    #drawCanvas { width: 256px; height: 128px; display: none; }
    
    .tab-btn { width: 30%; font-size: 12px; padding: 8px; }
    .active-tab { background-color: #00ff00; color: black; }
  </style>
</head>
<body>
  <h2>OLED Command Center</h2>
  
  <div class="panel">
    <h3>Built-in Aesthetics</h3>
    <button onclick="fetch('/set?mode=0')">Digital Rain</button>
    <button onclick="fetch('/set?mode=1')">Night City</button>
    <button onclick="fetch('/set?mode=2')">Sonar Ping</button>
    <button onclick="fetch('/set?mode=3')">Divergence</button>
    <button onclick="fetch('/set?mode=4')">Snowfall</button>
    <button onclick="fetch('/set?mode=6')">DVD Bounce</button>
  </div>

  <div class="panel">
    <h3>Media & Infographics Link</h3>
    
    <div>
      <button id="tabVideo" class="tab-btn active-tab" onclick="switchTab('video')">Camera/Media</button>
      <button id="tabDraw" class="tab-btn" onclick="switchTab('draw')">Draw</button>
    </div>

    <!-- VIDEO / CAMERA SECTION -->
    <div id="videoSection">
      <button onclick="startCamera()">Start Webcam</button>
      <label class="file-label">
        <input type="file" id="mediaInput" accept="video/*, image/*"> Upload Video/Img
      </label>
      
      <!-- Hidden Elements for Processing -->
      <video id="sourceVideo" autoplay loop muted playsinline style="display:none;"></video>
      <img id="sourceImage" style="display:none;">
      
      <br><label>Rotation: <span id="rotVal">0</span>°</label>
      <input type="range" id="rotation" min="0" max="360" value="0">
      
      <br><label>Zoom/Scale: <span id="zoomVal">1.0</span>x</label>
      <input type="range" id="zoom" min="0.1" max="5.0" step="0.1" value="1.0">
    </div>

    <!-- DRAWING SECTION -->
    <div id="drawSection" style="display:none;">
      <button onclick="clearCanvas()">Clear Board</button>
      <canvas id="drawCanvas" width="128" height="64"></canvas>
      <p style="font-size:11px; margin:0; color:#aaa;">Auto-syncs instantly to OLED as you draw.</p>
    </div>

    <!-- COMMON CONTROLS -->
    <br><label>Contrast Threshold: <span id="threshVal">128</span></label>
    <input type="range" id="threshold" min="0" max="255" value="128">

    <label style="display:inline-block; margin-top:10px;">
      <input type="checkbox" id="invertColors"> Invert Output Colors
    </label>

    <h4>OLED Preview (128x64)</h4>
    <canvas id="previewCanvas" width="128" height="64"></canvas>
    
    <button id="streamBtn" onclick="toggleStream()" style="width:100%; font-size:18px; padding:15px; margin-top:10px;">START LIVE STREAM</button>
    <p id="status" style="margin-top: 10px;"></p>
  </div>

  <script>
    const video = document.getElementById('sourceVideo');
    const image = document.getElementById('sourceImage');
    const previewCanvas = document.getElementById('previewCanvas');
    const previewCtx = previewCanvas.getContext('2d', { willReadFrequently: true });
    
    const drawCanvas = document.getElementById('drawCanvas');
    const drawCtx = drawCanvas.getContext('2d');
    
    // Setup drawing canvas
    drawCtx.fillStyle = "black";
    drawCtx.fillRect(0, 0, 128, 64);
    drawCtx.strokeStyle = "white";
    drawCtx.lineWidth = 3;
    drawCtx.lineCap = "round";
    drawCtx.lineJoin = "round";

    let finalBinaryData = new Uint8Array(1024); // Lightning-fast binary buffer
    let isStreaming = false;
    let currentTab = 'video';
    let isDrawing = false;
    let mediaType = 'none'; // 'video' or 'image'

    // Initialize WebSocket connection
    let ws = new WebSocket(`ws://${window.location.hostname}:81/`);
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => { document.getElementById('status').innerText = "WebSocket Connected! Link stable."; };
    ws.onclose = () => { document.getElementById('status').innerText = "WebSocket Disconnected. Refresh page."; };

    // --- Tab Switching ---
    function switchTab(tab) {
      currentTab = tab;
      document.getElementById('videoSection').style.display = tab === 'video' ? 'block' : 'none';
      document.getElementById('drawSection').style.display = tab === 'draw' ? 'block' : 'none';
      document.getElementById('tabVideo').className = tab === 'video' ? 'tab-btn active-tab' : 'tab-btn';
      document.getElementById('tabDraw').className = tab === 'draw' ? 'tab-btn active-tab' : 'tab-btn';
      document.getElementById('previewCanvas').style.display = tab === 'video' ? 'block' : 'none';
      document.getElementById('streamBtn').style.display = tab === 'video' ? 'block' : 'none';
    }

    // --- Drawing Logic ---
    function getMousePos(canvas, evt) {
      var rect = canvas.getBoundingClientRect();
      return {
        x: (evt.clientX - rect.left) / (rect.width / 128),
        y: (evt.clientY - rect.top) / (rect.height / 64)
      };
    }
    
    function startDrawing(e) {
      isDrawing = true;
      let pos = getMousePos(drawCanvas, e);
      drawCtx.beginPath();
      drawCtx.moveTo(pos.x, pos.y);
    }
    
    function draw(e) {
      if (!isDrawing) return;
      let pos = getMousePos(drawCanvas, e);
      drawCtx.lineTo(pos.x, pos.y);
      drawCtx.stroke();
    }
    
    function stopDrawing() {
      if(isDrawing && !isStreaming) {
        sendSingleFrame(); // Instantly update OLED when stroke ends
      }
      isDrawing = false;
    }
    
    drawCanvas.addEventListener('mousedown', startDrawing);
    drawCanvas.addEventListener('mousemove', draw);
    drawCanvas.addEventListener('mouseup', stopDrawing);
    drawCanvas.addEventListener('mouseleave', stopDrawing);

    drawCanvas.addEventListener('touchstart', e => { e.preventDefault(); startDrawing(e.touches[0]); }, {passive: false});
    drawCanvas.addEventListener('touchmove', e => { e.preventDefault(); draw(e.touches[0]); }, {passive: false});
    drawCanvas.addEventListener('touchend', e => { e.preventDefault(); stopDrawing(); }, {passive: false});

    function clearCanvas() {
      drawCtx.fillStyle = "black";
      drawCtx.fillRect(0, 0, 128, 64);
      sendSingleFrame(); // Instantly clear OLED
    }

    // --- Media Input Logic ---
    async function startCamera() {
      try {
        if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) throw new Error("Camera API blocked.");
        const stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment' } });
        mediaType = 'video';
        video.srcObject = stream;
        video.play();
        document.getElementById('status').innerText = "Camera Active!";
      } catch (err) {
        alert("Camera Blocked! Use Ngrok for an HTTPS link, or use the Upload Image button.");
      }
    }

    document.getElementById('mediaInput').addEventListener('change', function(e) {
      if(e.target.files.length > 0) {
        const file = e.target.files[0];
        const url = URL.createObjectURL(file);
        
        if (file.type.startsWith('image/')) {
          mediaType = 'image';
          video.pause();
          image.onload = () => { if (!isStreaming) sendSingleFrame(); }; 
          image.src = url;
          document.getElementById('status').innerText = "Image Loaded & Transmitted!";
        } else {
          mediaType = 'video';
          video.srcObject = null;
          video.src = url;
          video.play();
          document.getElementById('status').innerText = "Video Loaded! Press Start Live Stream.";
        }
      }
    });

    document.getElementById('rotation').addEventListener('input', e => { 
        document.getElementById('rotVal').innerText = e.target.value; 
        if(mediaType === 'image' && !isStreaming) sendSingleFrame(); 
    });

    document.getElementById('zoom').addEventListener('input', e => { 
        document.getElementById('zoomVal').innerText = e.target.value; 
        if(mediaType === 'image' && !isStreaming) sendSingleFrame(); 
    });
    
    document.getElementById('threshold').addEventListener('input', e => { 
        document.getElementById('threshVal').innerText = e.target.value; 
        if(!isStreaming) sendSingleFrame(); 
    });

    document.getElementById('invertColors').addEventListener('change', e => {
        if(!isStreaming) sendSingleFrame(); 
    });

    // --- Processing & Streaming Logic ---
    function processFrame() {
      const threshold = parseInt(document.getElementById('threshold').value);
      const invert = document.getElementById('invertColors').checked;
      
      if (currentTab === 'video') {
        const rot = parseInt(document.getElementById('rotation').value);
        const zoom = parseFloat(document.getElementById('zoom').value);
        previewCtx.fillStyle = "black";
        previewCtx.fillRect(0, 0, 128, 64);
        
        let mediaObj = mediaType === 'image' ? image : video;
        let w = mediaType === 'image' ? image.naturalWidth : video.videoWidth;
        let h = mediaType === 'image' ? image.naturalHeight : video.videoHeight;
        
        if(w > 0) {
          previewCtx.save();
          previewCtx.translate(64, 32);
          previewCtx.rotate(rot * Math.PI / 180);
          
          let ratio = Math.max(128 / w, 64 / h) * zoom; // Apply zoom multiplier
          let newW = w * ratio;
          let newH = h * ratio;
          previewCtx.drawImage(mediaObj, -newW/2, -newH/2, newW, newH);
          previewCtx.restore();
        }
      }

      const targetCtx = currentTab === 'video' ? previewCtx : drawCtx;
      const imgData = targetCtx.getImageData(0, 0, 128, 64);
      const data = imgData.data;
      
      let byteIndex = 0;
      for (let y = 0; y < 64; y++) {
        for (let x = 0; x < 128; x += 8) {
          let byte = 0;
          for (let bit = 0; bit < 8; bit++) {
            let i = ((y * 128) + (x + bit)) * 4;
            let brightness = (data[i] + data[i+1] + data[i+2]) / 3;
            
            // Apply Threshold and Invert Logic
            let isWhite = invert ? (brightness <= threshold) : (brightness > threshold);
            
            if (isWhite) { 
              byte |= (1 << (7 - bit));
              if(currentTab === 'video') { data[i]=255; data[i+1]=255; data[i+2]=255; }
            } else {
              if(currentTab === 'video') { data[i]=0; data[i+1]=0; data[i+2]=0; }
            }
          }
          finalBinaryData[byteIndex++] = byte; // WebSockets: Push raw bytes
        }
      }
      if(currentTab === 'video') targetCtx.putImageData(imgData, 0, 0);
    }

    async function sendSingleFrame() {
      processFrame();
      if(ws.readyState === WebSocket.OPEN) {
        ws.send(finalBinaryData); // Instant transmission
      }
    }

    async function sendFrame() {
      if (!isStreaming) return;
      processFrame();
      
      if(ws.readyState === WebSocket.OPEN) {
        ws.send(finalBinaryData); // Instant transmission
        document.getElementById('status').innerText = "Streaming: ACTIVE (" + new Date().toLocaleTimeString() + ")";
      }
      
      if(isStreaming) setTimeout(sendFrame, 66); // Faster loop! ~15 FPS
    }

    function toggleStream() {
      isStreaming = !isStreaming;
      const btn = document.getElementById('streamBtn');
      if (isStreaming) {
        btn.innerText = "STOP LIVE STREAM";
        btn.style.backgroundColor = "#ff0000";
        btn.style.color = "white";
        sendFrame(); 
      } else {
        btn.innerText = "START LIVE STREAM";
        btn.style.backgroundColor = "#1a1a1a";
        btn.style.color = "#00ff00";
        document.getElementById('status').innerText = "Stream Paused.";
      }
    }
    
    // Independent preview loop
    setInterval(() => {
      if(!isStreaming && currentTab === 'video') processFrame();
    }, 40);
  </script>
</body>
</html>
)rawliteral";

// --- NEW: WEBSOCKET EVENT HANDLER ---
// Bypasses HTTP entirely for direct binary memory copying
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_BIN && length == 1024) {
    memcpy(custom_bmp, payload, 1024); // Blazing fast memory transfer!
    currentMode = 5; 
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize random values for animations
  for(int x = 0; x < 21; x++) drops[x] = random(-64, 0);
  for(int i=0; i<30; i++) { snowX[i] = random(0,128); snowY[i] = random(0,64); }

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Connecting to WiFi...");
  display.display();

  // CONNECT TO HOME WIFI IN STATION MODE
  const char* ssid = "YOUR WIFI NAME";         // <--- CHANGE THIS TO YOUR WIFI NAME
  const char* password = "12345678"; // <--- CHANGE THIS TO YOUR WIFI PASSWORD

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password); 
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected to WiFi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // --- NEW: SETUP mDNS ---
  if (MDNS.begin("gadget")) { 
    Serial.println("mDNS responder started at http://gadget.local");
  }

  // --- NEW: SETUP WEBSOCKETS ---
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Show IP & DNS Address on OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.println("Portal Links:");
  display.println("");
  display.setTextSize(1);
  display.println("http://gadget.local");
  display.println(WiFi.localIP());
  display.display();
  delay(5000); 
  
  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });
  
  server.on("/set", []() {
    if (server.hasArg("mode")) {
      currentMode = server.arg("mode").toInt();
      display.clearDisplay(); 
      steinsStep = 0;
    }
    server.send(200, "text/plain", "Mode changed");
  });

  server.begin();
}

void loop() {
  MDNS.update();       // Handle Local DNS
  webSocket.loop();    // Handle WebSockets
  server.handleClient(); 
  
  if (millis() - lastFrameTime > frameDelay) {
    lastFrameTime = millis();
    drawAnimationFrame();
  }
}

// ==========================================
// NON-BLOCKING ANIMATION ENGINE
// ==========================================
void drawAnimationFrame() {
  if (currentMode != 5) display.clearDisplay();

  if (currentMode == 0) {
    frameDelay = 50;
    display.setTextSize(1);
    for(int x = 0; x < 21; x++) {
      display.setCursor(x * 6, drops[x]);
      display.print((char)random(33, 126));
      drops[x] += 8;
      if(drops[x] > 64) drops[x] = random(-16, 0);
    }
  } 
  else if (currentMode == 1) {
    frameDelay = 40;
    display.fillCircle(20, 8, 5, SSD1306_WHITE); 
    display.drawPixel(random(0, 128), random(0, 15), SSD1306_WHITE); 
    for(int i = 0; i < 10; i++) {
      int height = (i % 3 == 0) ? 30 : ((i % 2 == 0) ? 15 : 40);
      int xPos = (i * 15) - offset;
      if (xPos < -20) xPos += 150;
      display.fillRect(xPos, 64 - height, 12, height, SSD1306_WHITE);
    }
    offset += 2;
    if (offset > 150) offset = 0;
  }
  else if (currentMode == 2) {
    frameDelay = 30;
    display.fillRect(54, 10, 20, 6, SSD1306_WHITE); 
    display.fillRect(60, 4, 8, 6, SSD1306_WHITE);   
    display.drawLine(0, 15, 128, 15, SSD1306_WHITE); 
    display.drawCircle(64, 15, sonarRadius, SSD1306_WHITE);
    sonarRadius += 3;
    if (sonarRadius > 80) sonarRadius = 0;
  }
  else if (currentMode == 3) {
    display.setTextSize(2);
    if (steinsStep < 15) {
      frameDelay = 40;
      display.setCursor(16, 24);
      display.print(random(0, 2)); display.print("."); display.print(random(100000, 999999));
      steinsStep++;
    } else {
      frameDelay = 2000; 
      display.setCursor(16, 24);
      display.print("1.048596");
      steinsStep = 0; 
    }
  }
  else if (currentMode == 4) {
    frameDelay = 50;
    for(int i = 0; i < 30; i++) {
      display.drawPixel(snowX[i], snowY[i], SSD1306_WHITE);
      snowY[i] += random(1, 3);
      snowX[i] += random(-1, 2);
      if(snowY[i] > 64) { snowY[i] = 0; snowX[i] = random(0, 128); }
      if(snowX[i] < 0) snowX[i] = 128;
      if(snowX[i] > 128) snowX[i] = 0;
    }
  }
  else if (currentMode == 5) {
    frameDelay = 0; 
    display.clearDisplay(); 
    display.drawBitmap(0, 0, custom_bmp, 128, 64, SSD1306_WHITE);
  }
  else if (currentMode == 6) {
    frameDelay = 30;
    display.fillRect(boxX, boxY, 20, 10, SSD1306_WHITE);
    boxX += dirX;
    boxY += dirY;
    if(boxX <= 0 || boxX >= 128 - 20) dirX = -dirX;
    if(boxY <= 0 || boxY >= 64 - 10) dirY = -dirY;
  }

  display.display();
}
