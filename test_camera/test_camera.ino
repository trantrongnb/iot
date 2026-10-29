/*
 * THU CAMERA OV3660 QUA WI-FI - ESP32-S3 N16R8
 * Arduino-ESP32 3.x. Thong tin Wi-Fi nam trong secrets.h.
 *
 * Dien Wi-Fi trong secrets.h truoc khi nap.
 * Upload -> Serial Monitor 115200 -> mo http://<IP duoc in>/ tren trinh duyet.
 * May tinh/dien thoai can ket noi cung mang voi ESP32.
 * Serial: 's' = trang thai/IP; 'c' = thu chup JPEG, khong can trinh duyet.
 *
 * Arduino IDE: ESP32S3 Dev Module; Flash 16MB; PSRAM OPI PSRAM.
 * Khong dung loa/nut nhan/API trong sketch nay.
 *
 * Pinout ESP32-S3-EYE / Freenove da chup thanh cong tren bo cua ban.
 * Web port 80; MJPEG realtime port 81 (khong can API).
 * Neu bo khac, sua khoi CAM_* theo so do bo, khong tu doi day FPC camera.
 * Tham khao: github.com/Freenove/Freenove_ESP32_S3_WROOM_Board
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_camera.h>
#include <driver/gpio.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Hay chon ESP32S3 Dev Module"
#endif

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_MS = 15000;

// Pinout da chup JPEG thanh cong voi OV3660 tren bo dang dung.
constexpr int CAM_XCLK = 15;
constexpr int CAM_SDA = 4;
constexpr int CAM_SCL = 5;
constexpr int CAM_D0 = 11;
constexpr int CAM_D1 = 9;
constexpr int CAM_D2 = 8;
constexpr int CAM_D3 = 10;
constexpr int CAM_D4 = 12;
constexpr int CAM_D5 = 18;
constexpr int CAM_D6 = 17;
constexpr int CAM_D7 = 16;
constexpr int CAM_VSYNC = 6;
constexpr int CAM_HREF = 7;
constexpr int CAM_PCLK = 13;
constexpr int CAM_PWDN = -1;
constexpr int CAM_RESET = -1;
constexpr bool CAMERA_VFLIP = true;
constexpr bool CAMERA_HMIRROR = false;

constexpr uint16_t STREAM_PORT = 81;
constexpr uint32_t STREAM_INTERVAL_MS = 100;  // Gioi han toi da ~10 fps, tuy mang/camera.
WebServer server(80);
httpd_handle_t streamServer = nullptr;
SemaphoreHandle_t cameraMutex = nullptr;
std::atomic<bool> streamActive{false};
std::atomic<bool> streamStopRequested{false};
std::atomic<uint32_t> streamFrames{0};
bool cameraReady = false;
bool serverStarted = false;
bool wifiWasConnected = false;
uint32_t lastWifiAttempt = 0;
uint32_t photoCount = 0;
uint16_t sensorPid = 0;
esp_err_t cameraInitError = ESP_OK;

const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Thử camera ESP32</title>
<style>
body{font:16px system-ui,sans-serif;margin:24px auto;padding:0 16px;max-width:760px;background:#f4f6f8;color:#18232d}
h1{font-size:25px}button{padding:12px 18px;border:0;border-radius:8px;background:#1763c6;color:white;font:inherit;cursor:pointer}
button:disabled{opacity:.5}label{display:inline-block;margin:12px}img{display:block;max-width:100%;height:auto;margin:20px auto;border-radius:10px;background:#ddd}
#message{white-space:pre-wrap}a{color:#1763c6}
</style></head><body>
<h1>Thử camera ESP32-S3</h1>
<p id="hardware">Đang kiểm tra camera…</p>
<button id="shoot" disabled>Chụp ảnh mới</button>
<button id="live" disabled>Bật realtime</button>
<p id="message" role="status">Chưa chụp ảnh.</p>
<img id="photo" alt="Ảnh từ camera" hidden>
<p><a href="/capture" target="_blank" rel="noopener">Mở ảnh JPEG</a></p>
<script>
const shoot=document.getElementById('shoot'), live=document.getElementById('live');
const picture=document.getElementById('photo'), message=document.getElementById('message');
let working=false, previousUrl=null, ready=false, streaming=false, streamReady=false, streamPort=81;
function controls(){shoot.disabled=working||!ready;live.disabled=working||!ready||!streamReady;}
async function stopLive(){
  streaming=false;picture.removeAttribute('src');live.textContent='Bật realtime';
  const controller=new AbortController(), timeout=setTimeout(()=>controller.abort(),6000);
  try{
    const result=await fetch('/stop-stream',{method:'POST',signal:controller.signal});
    if(!result.ok)throw new Error('Không dừng được luồng video');
  }finally{clearTimeout(timeout);}
}
async function capture(){
  if(working||!ready)return;
  working=true;controls();
  const controller=new AbortController(), deadline=setTimeout(()=>controller.abort(),15000);
  try{
    if(streaming)await stopLive();
    message.textContent='Đang chụp ảnh…';
    const result=await fetch('/capture?t='+Date.now(),{cache:'no-store',signal:controller.signal});
    if(!result.ok)throw new Error(await result.text());
    const blob=await result.blob(), nextUrl=URL.createObjectURL(blob);
    if(previousUrl)URL.revokeObjectURL(previousUrl);
    previousUrl=nextUrl;picture.src=nextUrl;picture.hidden=false;
    message.textContent='Đã nhận ảnh '+Math.round(blob.size/1024)+' KB — '+new Date().toLocaleTimeString();
  }catch(error){message.textContent='Chụp chưa thành công: '+error.message;}
  finally{clearTimeout(deadline);working=false;controls();}
}
shoot.onclick=capture;
live.onclick=async()=>{
  if(working||!ready||!streamReady)return;
  working=true;controls();
  const wasStreaming=streaming;
  try{
    await stopLive();
    if(wasStreaming){message.textContent='Đã dừng realtime.';return;}
    if(previousUrl){URL.revokeObjectURL(previousUrl);previousUrl=null;}
    streaming=true;live.textContent='Dừng realtime';picture.hidden=false;
    picture.src='http://'+location.hostname+':'+streamPort+'/stream?t='+Date.now();
    message.textContent='Đang xem trực tiếp. Bấm Dừng realtime để dừng hoặc Chụp ảnh mới để lấy ảnh tĩnh.';
  }catch(error){message.textContent='Chưa bật được realtime: '+error.message;}
  finally{working=false;controls();}
};
picture.onerror=()=>{
  if(!streaming)return;
  stopLive().catch(()=>{});
  message.textContent='Luồng video đã ngắt. Kiểm tra Wi-Fi rồi bấm Bật realtime để kết nối lại.';
};
window.addEventListener('pagehide',()=>{
  if(streaming)fetch('/stop-stream',{method:'POST',keepalive:true}).catch(()=>{});
});
(async()=>{
  try{
    const result=await fetch('/status',{cache:'no-store'});
    if(!result.ok)throw new Error('Không đọc được trạng thái');
    const status=await result.json();ready=status.camera;streamReady=status.stream_ready;streamPort=status.stream_port;
    document.getElementById('hardware').textContent=
      status.sensor+' · '+(status.psram?'PSRAM đã nhận':'Ảnh 320×240, chưa nhận PSRAM')+' · Đã lật ảnh dọc';
    controls();
    if(ready)capture();
    else message.textContent='Camera chưa khởi tạo. Xem log Serial để kiểm tra chân camera và cáp kết nối.';
  }catch(error){message.textContent=error.message;}
})();
</script></body></html>)HTML";

const char *sensorName() {
  if (sensorPid == OV3660_PID) return "OV3660";
  if (sensorPid == OV5640_PID) return "OV5640";
  if (sensorPid == OV2640_PID) return "OV2640";
  return cameraReady ? "Camera" : "Camera chưa sẵn sàng";
}

bool validateCameraPins() {
  const int pins[] = {CAM_XCLK, CAM_SDA, CAM_SCL, CAM_D0, CAM_D1, CAM_D2,
      CAM_D3, CAM_D4, CAM_D5, CAM_D6, CAM_D7, CAM_VSYNC, CAM_HREF, CAM_PCLK,
      CAM_PWDN, CAM_RESET};
  for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
    const int pin = pins[i];
    if (i >= 14 && pin == -1) continue;
    const bool output = i < 3 || i >= 14;
    if (pin < 0 || !GPIO_IS_VALID_GPIO(pin) ||
        (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin)) || (pin >= 26 && pin <= 37)) {
      Serial.printf("[CONFIG] Chan camera GPIO%d khong hop le cho cau hinh S3 N16R8.\n", pin);
      return false;
    }
    for (size_t j = 0; j < i; ++j) {
      if (pin == pins[j]) {
        Serial.printf("[CONFIG] Trung GPIO%d trong pinout camera.\n", pin);
        return false;
      }
    }
  }
  return true;
}

bool initCamera() {
  if (!validateCameraPins()) {
    cameraInitError = ESP_ERR_INVALID_ARG;
    return false;
  }
  cameraMutex = xSemaphoreCreateMutex();
  if (!cameraMutex) {
    cameraInitError = ESP_ERR_NO_MEM;
    Serial.println("[CAMERA LOI] Khong tao duoc mutex camera.");
    return false;
  }
  const bool hasPsram = psramFound();
  Serial.printf("[PSRAM] %s, tong=%lu byte\n", hasPsram ? "OK" : "NO",
                static_cast<unsigned long>(ESP.getPsramSize()));
  if (!hasPsram)
    Serial.println("[PSRAM] Tam thu JPEG QVGA bang RAM trong. N16R8 nen bat Tools > PSRAM > OPI PSRAM.");
  Serial.println("[PINOUT] ESP32-S3-EYE/Freenove, da chup thanh cong tren bo nay.");
  Serial.printf("[PINOUT] XCLK=%d SDA=%d SCL=%d VSYNC=%d HREF=%d PCLK=%d\n",
                CAM_XCLK, CAM_SDA, CAM_SCL, CAM_VSYNC, CAM_HREF, CAM_PCLK);
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_D0; config.pin_d1 = CAM_D1;
  config.pin_d2 = CAM_D2; config.pin_d3 = CAM_D3;
  config.pin_d4 = CAM_D4; config.pin_d5 = CAM_D5;
  config.pin_d6 = CAM_D6; config.pin_d7 = CAM_D7;
  config.pin_xclk = CAM_XCLK;
  config.pin_pclk = CAM_PCLK;
  config.pin_vsync = CAM_VSYNC;
  config.pin_href = CAM_HREF;
  config.pin_sccb_sda = CAM_SDA;
  config.pin_sccb_scl = CAM_SCL;
  config.pin_pwdn = CAM_PWDN;
  config.pin_reset = CAM_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = hasPsram ? FRAMESIZE_VGA : FRAMESIZE_QVGA;
  config.jpeg_quality = hasPsram ? 12 : 16;
  config.fb_count = hasPsram ? 2 : 1;
  config.fb_location = hasPsram ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode = hasPsram ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  cameraInitError = esp_camera_init(&config);
  if (cameraInitError != ESP_OK) {
    Serial.printf("[CAMERA LOI] 0x%08X: %s\n", unsigned(cameraInitError), esp_err_to_name(cameraInitError));
    Serial.println("Kiem tra pinout bo va cap FPC camera; rut nguon truoc khi thao/lap cap.");
    return false;
  }
  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    sensorPid = sensor->id.PID;
    sensor->set_vflip(sensor, CAMERA_VFLIP);
    sensor->set_hmirror(sensor, CAMERA_HMIRROR);
  }
  Serial.printf("[IMAGE] VFLIP=%d HMIRROR=%d\n", CAMERA_VFLIP, CAMERA_HMIRROR);
  cameraReady = true;
  Serial.printf("[CAMERA OK] %s, PID=0x%04X, JPEG %s\n", sensorName(), sensorPid,
                hasPsram ? "640x480" : "320x240");
  return true;
}

// Mutex duoc giu tu luc lay frame den khi gui xong va tra buffer.
// Can thiet khi stream task va WebServer/Serial dung chung camera (ke ca fb_count=1).
void releasePhoto(camera_fb_t *frame) {
  if (frame) esp_camera_fb_return(frame);
  xSemaphoreGive(cameraMutex);
}

camera_fb_t *takePhoto(bool fresh = true) {
  if (!cameraReady || xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(2000)) != pdTRUE)
    return nullptr;
  if (fresh) {
    for (int i = 0; i < 2; ++i) {
      camera_fb_t *old = esp_camera_fb_get();
      if (!old) { releasePhoto(nullptr); return nullptr; }
      esp_camera_fb_return(old);
    }
  }
  camera_fb_t *frame = esp_camera_fb_get();
  if (!frame || frame->format != PIXFORMAT_JPEG || frame->len < 4 ||
      frame->buf[0] != 0xFF || frame->buf[1] != 0xD8) {
    releasePhoto(frame);
    return nullptr;
  }
  return frame;
}

// Luong MJPEG theo vi du CameraWebServer cua Espressif; server rieng port 81.
esp_err_t handleStream(httpd_req_t *request) {
  if (!cameraReady) {
    httpd_resp_set_status(request, "503 Service Unavailable");
    return httpd_resp_send(request, "Camera unavailable", HTTPD_RESP_USE_STRLEN);
  }
  streamStopRequested.store(false);
  streamActive.store(true);
  esp_err_t result = httpd_resp_set_type(request, "multipart/x-mixed-replace;boundary=esp32frame");
  httpd_resp_set_hdr(request, "Cache-Control", "no-store, no-cache, must-revalidate");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  Serial.println("[REALTIME] Bat dau MJPEG.");
  while (result == ESP_OK && !streamStopRequested.load() && WiFi.status() == WL_CONNECTED) {
    const uint32_t started = millis();
    camera_fb_t *frame = takePhoto(false);
    if (!frame) { result = ESP_FAIL; break; }
    char header[96];
    const int length = snprintf(header, sizeof(header),
        "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", unsigned(frame->len));
    result = httpd_resp_send_chunk(request, "\r\n--esp32frame\r\n", HTTPD_RESP_USE_STRLEN);
    if (result == ESP_OK) result = httpd_resp_send_chunk(request, header, length);
    if (result == ESP_OK) result = httpd_resp_send_chunk(request,
        reinterpret_cast<const char *>(frame->buf), frame->len);
    releasePhoto(frame);
    if (result == ESP_OK) streamFrames.fetch_add(1);
    const uint32_t elapsed = millis() - started;
    if (result == ESP_OK && elapsed < STREAM_INTERVAL_MS) delay(STREAM_INTERVAL_MS - elapsed);
  }
  streamActive.store(false);
  Serial.println("[REALTIME] Da dung MJPEG.");
  if (result == ESP_OK) result = httpd_resp_send_chunk(request, nullptr, 0);
  return result;
}

void startStreamServer() {
  if (!cameraReady || streamServer) return;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = 32769;
  config.stack_size = 6144;
  config.max_open_sockets = 3;
  config.lru_purge_enable = true;
  config.send_wait_timeout = 5;
  config.recv_wait_timeout = 5;
  esp_err_t result = httpd_start(&streamServer, &config);
  if (result == ESP_OK) {
    httpd_uri_t uri = {};
    uri.uri = "/stream";
    uri.method = HTTP_GET;
    uri.handler = handleStream;
    result = httpd_register_uri_handler(streamServer, &uri);
    if (result != ESP_OK) { httpd_stop(streamServer); streamServer = nullptr; }
  }
  if (result != ESP_OK) Serial.printf("[REALTIME LOI] %s\n", esp_err_to_name(result));
}

void testPhoto() {
  camera_fb_t *frame = takePhoto();
  if (!frame) {
    Serial.println("[CHUP LOI] Khong lay duoc JPEG. Kiem tra log camera/pinout/cap.");
    return;
  }
  Serial.printf("[CHUP OK] %ux%u, %u byte JPEG\n", unsigned(frame->width),
                unsigned(frame->height), unsigned(frame->len));
  releasePhoto(frame);
}

void handleCapture() {
  if (!cameraReady) {
    server.send(503, "text/plain; charset=utf-8", "Camera chưa sẵn sàng. Xem log Serial.");
    return;
  }
  camera_fb_t *frame = takePhoto();
  if (!frame) {
    server.send(503, "text/plain; charset=utf-8", "Không lấy được JPEG từ camera. Xem log Serial.");
    return;
  }
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server.sendHeader("Content-Disposition", "inline; filename=camera.jpg");
  server.setContentLength(frame->len);
  server.send(200, "image/jpeg", "");
  NetworkClient &client = server.client();
  client.setTimeout(5000);
  const uint32_t started = millis();
  size_t sent = 0;
  while (sent < frame->len && client.connected() && millis() - started < 15000) {
    const size_t bytes = min(size_t(1460), frame->len - sent);
    const size_t written = client.write(frame->buf + sent, bytes);
    if (!written) break;
    sent += written;
  }
  if (sent == frame->len) {
    ++photoCount;
    Serial.printf("[WEB CHUP OK] #%lu, %ux%u, %u byte\n", static_cast<unsigned long>(photoCount),
                  unsigned(frame->width), unsigned(frame->height), unsigned(frame->len));
  } else {
    Serial.printf("[WEB LOI] Moi gui %u/%u byte JPEG.\n", unsigned(sent), unsigned(frame->len));
    client.stop();
  }
  releasePhoto(frame);
}

void handleStatus() {
  String json = String("{\"camera\":") + (cameraReady ? "true" : "false") +
      ",\"sensor\":\"" + sensorName() + "\",\"pid\":" + String(sensorPid) +
      ",\"psram\":" + (psramFound() ? "true" : "false") +
      ",\"psram_bytes\":" + String(ESP.getPsramSize()) +
      ",\"camera_error\":" + String(unsigned(cameraInitError)) +
      ",\"photos\":" + String(photoCount) +
      ",\"stream_ready\":" + (streamServer ? "true" : "false") +
      ",\"stream_active\":" + (streamActive.load() ? "true" : "false") +
      ",\"stream_port\":" + String(STREAM_PORT) +
      ",\"stream_frames\":" + String(streamFrames.load()) + "}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

void printStatus() {
  Serial.printf("[STATUS] camera=%s sensor=%s PSRAM=%s freePSRAM=%lu\n",
                cameraReady ? "OK" : "NO", sensorName(), psramFound() ? "OK" : "NO",
                static_cast<unsigned long>(ESP.getFreePsram()));
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI OK] IP=%s, RSSI=%ld dBm\n", WiFi.localIP().toString().c_str(), long(WiFi.RSSI()));
    Serial.printf("[MO TRINH DUYET] http://%s/\n", WiFi.localIP().toString().c_str());
    if (streamServer) Serial.printf("[REALTIME] http://%s:%u/stream active=%d frames=%lu\n",
        WiFi.localIP().toString().c_str(), STREAM_PORT, streamActive.load(),
        static_cast<unsigned long>(streamFrames.load()));
  } else Serial.printf("[WIFI] Chua ket noi, status=%d.\n", int(WiFi.status()));
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nTEST CAMERA OV3660 - ESP32-S3 - WI-FI PTIT");
  Serial.println("Serial: 's' xem IP/trang thai; 'c' thu chup JPEG.");
  initCamera();
  if (cameraReady) {
    delay(300);  // Cho cam bien on dinh phoi sang truoc anh thu dau tien.
    testPhoto();
  }
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  Serial.printf("[WIFI] Dang ket noi %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED && millis() - lastWifiAttempt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/stop-stream", HTTP_POST, []() {
    streamStopRequested.store(true);
    const uint32_t started = millis();
    while (streamActive.load() && millis() - started < 5500) delay(10);
    if (streamActive.load()) server.send(503, "text/plain", "Stream is still stopping. Retry.");
    else server.send(204, "text/plain", "");
  });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  printStatus();
}

void loop() {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    if (!serverStarted) {
      server.begin();
      serverStarted = true;
      startStreamServer();
      Serial.println("[WEB OK] Web camera san sang.");
    }
    if (!wifiWasConnected) printStatus();
    server.handleClient();
  } else if (millis() - lastWifiAttempt >= WIFI_RETRY_MS) {
    lastWifiAttempt = millis();
    Serial.println("[WIFI] Thu ket noi lai...");
    WiFi.reconnect();
  }
  wifiWasConnected = connected;
  while (Serial.available()) {
    const char command = char(Serial.read());
    if (command == 's' || command == 'S') printStatus();
    else if (command == 'c' || command == 'C') testPhoto();
  }
  delay(5);
}
