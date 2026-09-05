# WiFi Setup Guide

This project has two Arduino sketches, and both require your WiFi name and password before upload.

## 1) Open the sketch you want to use

- ESP32-S3 + TFT:
  `/home/runner/work/ESP-Dispaly-Stream/ESP-Dispaly-Stream/ESP32S3_spi_2.4_TFT/TFT_experiment/TFT_experiment.ino`
- ESP8266 + OLED:
  `/home/runner/work/ESP-Dispaly-Stream/ESP-Dispaly-Stream/ESP8266_i2c_0.96_Oled/OLED_experiment/OLED_experiment.ino`

## 2) Update WiFi credentials

### For ESP32-S3 sketch
Find:

```cpp
const char* ssid     = "YOUR WIFI NAME";
const char* password = "WiFi Password";
```

Replace with your actual WiFi network:

```cpp
const char* ssid     = "YourWiFiName";
const char* password = "YourWiFiPassword";
```

### For ESP8266 sketch
Find:

```cpp
const char* ssid = "YOUR WIFI NAME";
const char* password = "12345678";
```

Replace with your actual WiFi network:

```cpp
const char* ssid = "YourWiFiName";
const char* password = "YourWiFiPassword";
```

## 3) Upload and monitor connection

1. Select the correct board and COM port in Arduino IDE.
2. Upload the sketch.
3. Open Serial Monitor at `115200` baud.
4. Wait for connection logs and note the shown IP address.

## 4) Open the web portal

- Try: `http://gadget.local`
- If mDNS does not work on your network, use the IP address printed in Serial Monitor.

## Notes

- Use only 2.4 GHz WiFi for ESP boards.
- Keep credentials private and do not commit real passwords into public repositories.
