/*
 * ============================================================
 *  ESP32-CAM Optimized MJPEG Streaming Server
 *  - Static IP Configuration
 *  - Wi-Fi Auto Reconnect
 *  - Wi-Fi Sleep Disabled (Low Latency)
 *  - Max TX Power
 *  - FPS Monitoring
 *  - Fullscreen Landscape Web Interface
 *  - ✅ Login Access (Session Token)
 *  - Brownout Detection Handling
 * ============================================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// =============================================================
//  KONFIGURASI
// =============================================================

const char* ssid     = "NAMA_WIFI_ANDA";
const char* password = "PASSWORD_WIFI_ANDA";

// ⚠️ GANTI USERNAME & PASSWORD LOGIN SESUAI KEINGINAN ANDA
const char* CAM_USER = "admin";
const char* CAM_PASS = "esp32cam";

// Token session (akan di-generate saat login berhasil)
// Karakter random, tidak perlu diubah
const char* SESSION_SECRET = "xK9mP3qR7vL2nW5j";

IPAddress local_IP(192, 168, 1, 100);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

#define STREAM_FRAME_SIZE   FRAMESIZE_VGA
#define STREAM_JPEG_QUALITY 12
#define STREAM_FB_COUNT     2
#define MAX_WIFI_RETRY      40
#define WIFI_CHECK_INTERVAL 10000

// Max percobaan login salah sebelum lockout
#define MAX_LOGIN_ATTEMPTS  5
#define LOCKOUT_DURATION_MS 60000  // 60 detik lockout

// =============================================================
//  PIN KAMERA - AI-THINKER ESP32-CAM
// =============================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define FLASH_LED_PIN      4

// =============================================================
//  GLOBAL VARIABLES
// =============================================================
WebServer server(80);

volatile unsigned long frameCount    = 0;
volatile unsigned long lastFPSTime   = 0;
volatile int           currentFPS    = 0;

unsigned long lastWiFiCheck          = 0;
int           wifiReconnectCount     = 0;

// Session token yang aktif (hanya 1 session)
String activeSessionToken            = "";

// Login protection
int           loginAttempts          = 0;
unsigned long lockoutUntil           = 0;

// =============================================================
//  UTILITY: GENERATE SESSION TOKEN
// =============================================================
String generateToken() {
  String token = "";
  const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  // Seed dengan millis + analog noise
  randomSeed(millis() ^ analogRead(33));
  for (int i = 0; i < 32; i++) {
    token += charset[random(0, sizeof(charset) - 1)];
  }
  return token;
}

// =============================================================
//  UTILITY: CEK SESSION TOKEN DARI COOKIE
// =============================================================
bool isAuthenticated() {
  if (activeSessionToken == "") return false;

  // Baca cookie dari header request
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    String search = "session=" + activeSessionToken;
    if (cookie.indexOf(search) != -1) {
      return true;
    }
  }
  return false;
}

// =============================================================
//  UTILITY: REDIRECT KE LOGIN
// =============================================================
void redirectToLogin() {
  server.sendHeader("Location", "/login");
  server.sendHeader("Cache-Control", "no-cache");
  server.send(302, "text/plain", "Redirecting to login...");
}

// =============================================================
//  WIFI SETUP
// =============================================================
void setupWiFi() {
  Serial.println("\n[WiFi] Memulai konfigurasi...");
  WiFi.mode(WIFI_STA);
  WiFi.persistent(true);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("[WiFi] Static IP gagal, menggunakan DHCP...");
  }

  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin(ssid, password);

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < MAX_WIFI_RETRY) {
    delay(500);
    Serial.print(".");
    retryCount++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] Gagal! Restart...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("\n[WiFi] TERHUBUNG!");
  Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
}

void checkWiFiConnection() {
  unsigned long now = millis();
  if (now - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
    lastWiFiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      wifiReconnectCount++;
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
      int retry = 0;
      while (WiFi.status() != WL_CONNECTED && retry < 20) {
        delay(500); retry++;
      }
      if (wifiReconnectCount >= 5) ESP.restart();
    }
  }
}

// =============================================================
//  KAMERA SETUP
// =============================================================
bool setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.frame_size   = STREAM_FRAME_SIZE;
  config.jpeg_quality = STREAM_JPEG_QUALITY;
  config.fb_count     = STREAM_FB_COUNT;
  config.fb_location  = CAMERA_FB_IN_PSRAM;

  if (esp_camera_init(&config) != ESP_OK) return false;

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);   s->set_contrast(s, 1);
    s->set_saturation(s, 0);   s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);     s->set_wb_mode(s, 0);
    s->set_exposure_ctrl(s, 1);s->set_aec2(s, 0);
    s->set_gain_ctrl(s, 1);    s->set_agc_gain(s, 0);
    s->set_gainceiling(s, (gainceiling_t)6);
    s->set_bpc(s, 1);          s->set_wpc(s, 1);
    s->set_raw_gma(s, 1);      s->set_lenc(s, 0);
    s->set_hmirror(s, 0);      s->set_vflip(s, 0);
    s->set_dcw(s, 1);          s->set_colorbar(s, 0);
  }
  return true;
}

// =============================================================
//  MJPEG STREAMING HANDLER (dilindungi session)
// =============================================================
void handleStream() {
  if (!isAuthenticated()) {
    redirectToLogin();
    return;
  }

  WiFiClient client = server.client();
  client.print("HTTP/1.1 200 OK\r\n"
               "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
               "Access-Control-Allow-Origin: *\r\n"
               "Cache-Control: no-cache\r\n"
               "Connection: keep-alive\r\n\r\n");

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) continue;

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    size_t w = client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);
    if (!w) break;

    frameCount++;
    unsigned long now = millis();
    if (now - lastFPSTime >= 1000) {
      currentFPS = frameCount;
      frameCount = 0;
      lastFPSTime = now;
    }
    yield();
  }
}

// =============================================================
//  HALAMAN LOGIN (HTML)
// =============================================================
const char LOGIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
  <meta name="mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <title>ESP32-CAM &mdash; Login</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}

    html,body{
      width:100%;height:100%;
      background:#0a0a1a;
      font-family:'Segoe UI',Arial,sans-serif;
      color:#e0e0e0;
      display:flex;
      align-items:center;
      justify-content:center;
      min-height:100vh;
      overflow:hidden;
    }

    /* Background animasi */
    body::before{
      content:'';
      position:fixed;
      top:-50%;left:-50%;
      width:200%;height:200%;
      background:
        radial-gradient(ellipse at 20% 50%, rgba(33,150,243,0.08) 0%, transparent 50%),
        radial-gradient(ellipse at 80% 20%, rgba(103,58,183,0.08) 0%, transparent 50%),
        radial-gradient(ellipse at 50% 80%, rgba(0,188,212,0.06) 0%, transparent 50%);
      animation:bgMove 12s ease-in-out infinite alternate;
      z-index:0;
    }

    @keyframes bgMove{
      0%  {transform:translate(0,0) rotate(0deg)}
      100%{transform:translate(2%,2%) rotate(2deg)}
    }

    .card{
      position:relative;z-index:1;
      background:rgba(255,255,255,0.04);
      backdrop-filter:blur(20px);
      -webkit-backdrop-filter:blur(20px);
      border:1px solid rgba(255,255,255,0.1);
      border-radius:20px;
      padding:40px 36px;
      width:90%;max-width:380px;
      box-shadow:0 8px 40px rgba(0,0,0,0.5);
    }

    .logo{
      text-align:center;
      margin-bottom:28px;
    }

    .logo-icon{
      font-size:48px;
      display:block;
      margin-bottom:12px;
      animation:camFloat 3s ease-in-out infinite;
    }

    @keyframes camFloat{
      0%,100%{transform:translateY(0)}
      50%{transform:translateY(-6px)}
    }

    .logo h1{
      font-size:1.4em;
      font-weight:700;
      color:#7eb8ff;
      letter-spacing:1px;
    }

    .logo p{
      font-size:0.8em;
      color:#555;
      margin-top:4px;
    }

    /* Form */
    .form-group{
      margin-bottom:18px;
      position:relative;
    }

    .form-group label{
      display:block;
      font-size:0.78em;
      color:#888;
      margin-bottom:7px;
      text-transform:uppercase;
      letter-spacing:0.5px;
    }

    .input-wrap{
      position:relative;
      display:flex;
      align-items:center;
    }

    .input-icon{
      position:absolute;
      left:14px;
      font-size:16px;
      color:#555;
      pointer-events:none;
    }

    .form-group input{
      width:100%;
      padding:13px 14px 13px 42px;
      background:rgba(255,255,255,0.06);
      border:1px solid rgba(255,255,255,0.12);
      border-radius:10px;
      color:#fff;
      font-size:0.95em;
      outline:none;
      transition:all 0.3s ease;
      -webkit-appearance:none;
    }

    .form-group input:focus{
      border-color:#2196F3;
      background:rgba(33,150,243,0.08);
      box-shadow:0 0 0 3px rgba(33,150,243,0.15);
    }

    /* Toggle password visibility */
    .toggle-pw{
      position:absolute;
      right:12px;
      background:none;border:none;
      color:#555;font-size:18px;
      cursor:pointer;padding:4px;
    }

    /* Login button */
    .btn-login{
      width:100%;
      padding:14px;
      background:linear-gradient(135deg,#2196F3,#1565C0);
      border:none;border-radius:12px;
      color:#fff;font-size:1em;font-weight:700;
      cursor:pointer;
      transition:all 0.3s ease;
      box-shadow:0 4px 20px rgba(33,150,243,0.3);
      margin-top:6px;
      position:relative;
      overflow:hidden;
    }

    .btn-login::after{
      content:'';
      position:absolute;
      top:50%;left:50%;
      width:0;height:0;
      background:rgba(255,255,255,0.15);
      border-radius:50%;
      transform:translate(-50%,-50%);
      transition:width 0.4s ease,height 0.4s ease;
    }

    .btn-login:active::after{width:300px;height:300px}
    .btn-login:hover{transform:translateY(-2px);box-shadow:0 6px 25px rgba(33,150,243,0.4)}
    .btn-login:active{transform:translateY(0)}

    /* Loading state */
    .btn-login.loading{
      pointer-events:none;
      opacity:0.7;
    }

    .spinner-inline{
      display:inline-block;
      width:16px;height:16px;
      border:2px solid rgba(255,255,255,0.3);
      border-top:2px solid #fff;
      border-radius:50%;
      animation:spin 0.8s linear infinite;
      vertical-align:middle;
      margin-right:6px;
    }

    @keyframes spin{to{transform:rotate(360deg)}}

    /* Alert messages */
    .alert{
      padding:12px 16px;
      border-radius:10px;
      margin-bottom:18px;
      font-size:0.85em;
      display:none;
      align-items:center;
      gap:8px;
    }

    .alert.error{
      background:rgba(244,67,54,0.12);
      border:1px solid rgba(244,67,54,0.3);
      color:#ef9a9a;
      display:flex;
    }

    .alert.lockout{
      background:rgba(255,152,0,0.12);
      border:1px solid rgba(255,152,0,0.3);
      color:#ffcc80;
      display:flex;
    }

    .alert.success{
      background:rgba(76,175,80,0.12);
      border:1px solid rgba(76,175,80,0.3);
      color:#a5d6a7;
      display:flex;
    }

    /* Attempt counter */
    .attempt-info{
      text-align:center;
      font-size:0.75em;
      color:#555;
      margin-top:14px;
    }

    .attempt-info span{
      color:#ff7043;
    }

    /* Footer */
    .card-footer{
      text-align:center;
      margin-top:20px;
      font-size:0.72em;
      color:#444;
    }
  </style>
</head>
<body>

<div class="card">

  <div class="logo">
    <span class="logo-icon">&#x1F4F9;</span>
    <h1>ESP32-CAM</h1>
    <p>Secure Monitoring Access</p>
  </div>

  <!-- Alert area -->
  <div class="alert" id="alertBox">
    <span id="alertIcon">&#x26A0;</span>
    <span id="alertMsg">Pesan error di sini</span>
  </div>

  <form id="loginForm" onsubmit="doLogin(event)">

    <!-- Username -->
    <div class="form-group">
      <label>Username</label>
      <div class="input-wrap">
        <span class="input-icon">&#x1F464;</span>
        <input type="text" id="username" placeholder="Masukkan username"
               autocomplete="username" autocorrect="off"
               autocapitalize="none" spellcheck="false" required>
      </div>
    </div>

    <!-- Password -->
    <div class="form-group">
      <label>Password</label>
      <div class="input-wrap">
        <span class="input-icon">&#x1F512;</span>
        <input type="password" id="password" placeholder="Masukkan password"
               autocomplete="current-password" required>
        <button type="button" class="toggle-pw" onclick="togglePw()" id="toggleBtn">
          &#x1F441;
        </button>
      </div>
    </div>

    <button type="submit" class="btn-login" id="btnLogin">
      &#x1F510; Masuk
    </button>

  </form>

  <div class="attempt-info" id="attemptInfo" style="display:none">
    Percobaan salah: <span id="attemptCount">0</span> / 5
  </div>

  <div class="card-footer">
    ESP32-CAM Secure Stream &copy; 2026
  </div>

</div>

<script>
  let pwVisible = false;

  function togglePw(){
    pwVisible = !pwVisible;
    const inp = document.getElementById('password');
    const btn = document.getElementById('toggleBtn');
    inp.type = pwVisible ? 'text' : 'password';
    btn.innerHTML = pwVisible ? '&#x1F648;' : '&#x1F441;';
  }

  function showAlert(type, msg){
    const box = document.getElementById('alertBox');
    const msgEl = document.getElementById('alertMsg');
    box.className = 'alert ' + type;
    msgEl.textContent = msg;
    box.style.display = 'flex';
  }

  function hideAlert(){
    document.getElementById('alertBox').style.display = 'none';
  }

  async function doLogin(e){
    e.preventDefault();

    const user = document.getElementById('username').value.trim();
    const pass = document.getElementById('password').value;
    const btn  = document.getElementById('btnLogin');

    if(!user || !pass){
      showAlert('error', 'Username dan password tidak boleh kosong.');
      return;
    }

    // Loading state
    btn.classList.add('loading');
    btn.innerHTML = '<span class="spinner-inline"></span> Memverifikasi...';
    hideAlert();

    try {
      const res = await fetch('/auth', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'user=' + encodeURIComponent(user) + '&pass=' + encodeURIComponent(pass)
      });

      const data = await res.json();

      if(data.success){
        // Simpan token ke cookie
        document.cookie = 'session=' + data.token + '; path=/; SameSite=Strict';
        showAlert('success', 'Login berhasil! Mengalihkan...');
        btn.innerHTML = '&#x2705; Berhasil!';

        setTimeout(()=>{ window.location.href = '/'; }, 1000);

      } else if(data.locked){
        const sisa = Math.ceil(data.remaining / 1000);
        showAlert('lockout', `Terlalu banyak percobaan. Tunggu ${sisa} detik.`);
        btn.classList.remove('loading');
        btn.innerHTML = '&#x1F510; Masuk';

      } else {
        showAlert('error', data.message || 'Username atau password salah.');
        updateAttemptInfo(data.attempts);
        btn.classList.remove('loading');
        btn.innerHTML = '&#x1F510; Masuk';
        // Shake input
        document.getElementById('password').value = '';
        document.getElementById('password').focus();
      }
    } catch(err) {
      showAlert('error', 'Tidak dapat terhubung ke server.');
      btn.classList.remove('loading');
      btn.innerHTML = '&#x1F510; Masuk';
    }
  }

  function updateAttemptInfo(count){
    if(count > 0){
      document.getElementById('attemptInfo').style.display = 'block';
      document.getElementById('attemptCount').textContent = count;
    }
  }
</script>

</body>
</html>
)rawliteral";

// =============================================================
//  HALAMAN UTAMA / DASHBOARD (dilindungi session)
// =============================================================
const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
  <meta name="mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <title>ESP32-CAM Monitor</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}

    html,body{
      width:100%;height:100%;
      overflow:hidden;
      background:#000;
      font-family:'Segoe UI',Arial,sans-serif;
      color:#fff;
      touch-action:manipulation;
      -webkit-user-select:none;
      user-select:none;
    }

    #stream{
      position:fixed;
      top:0;left:0;
      width:100vw;height:100vh;
      object-fit:contain;
      background:#000;
      display:block;
      z-index:1;
    }

    #stream.fill-mode{object-fit:cover}

    /* ===== TOP BAR ===== */
    .top-bar{
      position:fixed;top:0;left:0;right:0;
      display:flex;justify-content:space-between;align-items:center;
      padding:8px 16px;
      background:linear-gradient(180deg,rgba(0,0,0,0.78) 0%,transparent 100%);
      z-index:100;
      transition:opacity 0.4s ease;
    }

    .top-bar.hidden{opacity:0;pointer-events:none}

    .live-badge{
      display:flex;align-items:center;gap:6px;
      background:rgba(0,0,0,0.3);
      border:1px solid rgba(255,255,255,0.15);
      padding:5px 12px;border-radius:20px;
      font-size:12px;font-weight:600;
    }

    .live-dot{width:8px;height:8px;border-radius:50%;background:#f44336}

    .live-dot.on{
      background:#4CAF50;
      box-shadow:0 0 8px #4CAF50;
      animation:pulse 2s infinite;
    }

    @keyframes pulse{
      0%,100%{box-shadow:0 0 4px #4CAF50}
      50%{box-shadow:0 0 12px #4CAF50}
    }

    .info-group{
      display:flex;gap:14px;align-items:center;
      font-family:'Courier New',monospace;
      font-size:12px;color:#aaa;
    }

    .info-group .val{color:#7eb8ff}

    /* Logout button di top bar */
    .btn-logout{
      background:rgba(244,67,54,0.15);
      border:1px solid rgba(244,67,54,0.35);
      color:#ef9a9a;
      padding:5px 12px;border-radius:20px;
      font-size:12px;font-weight:600;
      cursor:pointer;
      transition:all 0.2s;
    }

    .btn-logout:hover{background:rgba(244,67,54,0.3)}

    /* ===== BOTTOM BAR ===== */
    .bottom-bar{
      position:fixed;bottom:0;left:0;right:0;
      display:flex;justify-content:center;align-items:center;
      gap:8px;padding:12px 16px;
      background:linear-gradient(0deg,rgba(0,0,0,0.8) 0%,transparent 100%);
      z-index:100;
      transition:opacity 0.4s ease;
      flex-wrap:wrap;
    }

    .bottom-bar.hidden{opacity:0;pointer-events:none}

    .cbtn{
      padding:10px 16px;
      border:1px solid rgba(255,255,255,0.15);
      border-radius:10px;
      background:rgba(255,255,255,0.08);
      backdrop-filter:blur(10px);
      -webkit-backdrop-filter:blur(10px);
      color:#fff;font-size:13px;font-weight:600;
      cursor:pointer;
      transition:all 0.2s ease;
      display:flex;align-items:center;gap:6px;
      white-space:nowrap;
    }

    .cbtn:active{transform:scale(0.93);background:rgba(255,255,255,0.2)}
    .cbtn.active{background:rgba(76,175,80,0.3);border-color:#4CAF50}
    .cbtn.danger{background:rgba(244,67,54,0.2);border-color:rgba(244,67,54,0.5)}
    .cbtn .icon{font-size:16px}

    .res-select{
      background:rgba(255,255,255,0.08);
      backdrop-filter:blur(10px);
      -webkit-backdrop-filter:blur(10px);
      color:#fff;
      border:1px solid rgba(255,255,255,0.15);
      border-radius:10px;
      padding:10px 12px;
      font-size:13px;font-weight:600;
      cursor:pointer;outline:none;
      -webkit-appearance:none;
    }

    .res-select option{background:#222;color:#fff}

    /* ===== TOAST ===== */
    .toast{
      position:fixed;top:50%;left:50%;
      transform:translate(-50%,-50%) scale(0.8);
      background:rgba(0,0,0,0.85);
      backdrop-filter:blur(20px);
      -webkit-backdrop-filter:blur(20px);
      border:1px solid rgba(255,255,255,0.1);
      padding:16px 28px;border-radius:14px;
      font-size:15px;font-weight:600;
      z-index:999;opacity:0;
      transition:all 0.3s ease;pointer-events:none;
      text-align:center;
    }

    .toast.show{opacity:1;transform:translate(-50%,-50%) scale(1)}

    /* ===== START SCREEN ===== */
    .start-screen{
      position:fixed;top:0;left:0;width:100%;height:100%;
      background:radial-gradient(ellipse at center,#1a1a3e 0%,#0a0a1a 100%);
      display:flex;flex-direction:column;
      align-items:center;justify-content:center;
      z-index:500;transition:opacity 0.5s ease;
    }

    .start-screen.hidden{opacity:0;pointer-events:none}

    .start-screen h1{font-size:1.8em;color:#7eb8ff;margin-bottom:6px}
    .start-screen .welcome{
      font-size:0.85em;color:#4CAF50;margin-bottom:6px;
    }
    .start-screen p{color:#555;font-size:0.85em;margin-bottom:28px}

    .start-btn{
      padding:16px 48px;
      background:linear-gradient(135deg,#2196F3,#1565C0);
      border:none;border-radius:14px;
      color:#fff;font-size:1.1em;font-weight:700;
      cursor:pointer;
      box-shadow:0 4px 20px rgba(33,150,243,0.4);
      transition:all 0.3s ease;
    }

    .start-btn:active{transform:scale(0.95)}

    .tip{margin-top:20px;color:#444;font-size:0.75em;max-width:300px;text-align:center;line-height:1.5}

    /* ===== LOGOUT CONFIRM ===== */
    .confirm-overlay{
      position:fixed;top:0;left:0;width:100%;height:100%;
      background:rgba(0,0,0,0.7);
      backdrop-filter:blur(10px);
      -webkit-backdrop-filter:blur(10px);
      display:none;align-items:center;justify-content:center;
      z-index:600;
    }

    .confirm-overlay.show{display:flex}

    .confirm-box{
      background:rgba(20,20,40,0.95);
      border:1px solid rgba(255,255,255,0.1);
      border-radius:18px;
      padding:30px 28px;
      width:90%;max-width:320px;
      text-align:center;
    }

    .confirm-box h3{font-size:1.1em;color:#ff7043;margin-bottom:10px}
    .confirm-box p{font-size:0.85em;color:#888;margin-bottom:22px}

    .confirm-btns{display:flex;gap:10px;justify-content:center}

    .cbtn-confirm{
      padding:10px 24px;border-radius:10px;
      border:none;font-size:0.9em;font-weight:600;
      cursor:pointer;transition:all 0.2s;
    }

    .cbtn-yes{background:#f44336;color:#fff}
    .cbtn-yes:hover{background:#d32f2f}
    .cbtn-no{background:rgba(255,255,255,0.1);color:#fff;border:1px solid rgba(255,255,255,0.15)}
    .cbtn-no:hover{background:rgba(255,255,255,0.2)}

    @media(max-height:400px){
      .bottom-bar{padding:6px 10px;gap:6px}
      .cbtn{padding:7px 10px;font-size:11px}
      .cbtn .label{display:none}
      .res-select{padding:7px 8px;font-size:11px}
    }

    @media(max-width:480px) and (orientation:portrait){
      .cbtn .label{display:none}
    }
  </style>
</head>
<body>

  <!-- ===== START SCREEN ===== -->
  <div class="start-screen" id="startScreen">
    <h1>&#x1F4F9; ESP32-CAM</h1>
    <p class="welcome" id="welcomeMsg">&#x2705; Login Berhasil</p>
    <p>Fullscreen Landscape Monitor</p>
    <button class="start-btn" onclick="initStream()">&#x25B6; Mulai Streaming</button>
    <div class="tip">Putar smartphone ke landscape, lalu tekan Fullscreen untuk monitoring optimal.</div>
  </div>

  <!-- ===== STREAM ===== -->
  <img id="stream" src="" alt="">

  <!-- ===== TOP BAR ===== -->
  <div class="top-bar hidden" id="topBar">
    <div class="live-badge">
      <div class="live-dot" id="liveDot"></div>
      <span id="liveText">CONNECTING</span>
    </div>
    <div class="info-group">
      <span>FPS:<span class="val" id="fpsVal">--</span></span>
      <span>RSSI:<span class="val" id="rssiVal">--</span></span>
      <span id="resLabel">VGA</span>
    </div>
    <button class="btn-logout" onclick="confirmLogout()">&#x1F6AA; Logout</button>
  </div>

  <!-- ===== BOTTOM BAR ===== -->
  <div class="bottom-bar hidden" id="bottomBar">
    <select class="res-select" id="resSel" onchange="changeRes(this.value)">
      <option value="13">UXGA 1600x1200</option>
      <option value="12">SXGA 1280x1024</option>
      <option value="11">HD 1280x720</option>
      <option value="10">XGA 1024x768</option>
      <option value="9">SVGA 800x600</option>
      <option value="8" selected>VGA 640x480</option>
      <option value="6">HVGA 480x320</option>
      <option value="5">QVGA 320x240</option>
    </select>
    <button class="cbtn" onclick="toggleFit()" id="btnFit">
      <span class="icon">&#x2922;</span><span class="label">Fill</span>
    </button>
    <button class="cbtn" onclick="goFullscreen()" id="btnFS">
      <span class="icon">&#x26F6;</span><span class="label">Fullscreen</span>
    </button>
    <button class="cbtn" onclick="toggleFlash()" id="btnFlash">
      <span class="icon">&#x1F4A1;</span><span class="label">Flash</span>
    </button>
    <button class="cbtn" onclick="capturePhoto()">
      <span class="icon">&#x1F4F7;</span><span class="label">Capture</span>
    </button>
    <button class="cbtn danger" onclick="stopStream()">
      <span class="icon">&#x23F9;</span><span class="label">Stop</span>
    </button>
  </div>

  <!-- ===== LOGOUT CONFIRM DIALOG ===== -->
  <div class="confirm-overlay" id="confirmOverlay">
    <div class="confirm-box">
      <h3>&#x1F6AA; Konfirmasi Logout</h3>
      <p>Yakin ingin keluar dari sesi monitoring ini?</p>
      <div class="confirm-btns">
        <button class="cbtn-confirm cbtn-yes" onclick="doLogout()">Ya, Logout</button>
        <button class="cbtn-confirm cbtn-no"  onclick="closeConfirm()">Batal</button>
      </div>
    </div>
  </div>

  <!-- ===== TOAST ===== -->
  <div class="toast" id="toast"></div>

<script>
  const stream    = document.getElementById('stream');
  const topBar    = document.getElementById('topBar');
  const bottomBar = document.getElementById('bottomBar');
  const startScr  = document.getElementById('startScreen');
  const liveDot   = document.getElementById('liveDot');
  const liveText  = document.getElementById('liveText');
  const fpsVal    = document.getElementById('fpsVal');
  const rssiVal   = document.getElementById('rssiVal');
  const resLabel  = document.getElementById('resLabel');
  const btnFlash  = document.getElementById('btnFlash');
  const btnFit    = document.getElementById('btnFit');
  const toastEl   = document.getElementById('toast');

  let flashOn     = false;
  let fillMode    = false;
  let uiVisible   = true;
  let hideTimer   = null;
  let statusTimer = null;
  let streaming   = false;

  /* ===== TOAST ===== */
  function toast(msg, ms){
    toastEl.textContent = msg;
    toastEl.classList.add('show');
    setTimeout(()=> toastEl.classList.remove('show'), ms || 1500);
  }

  /* ===== INIT STREAM ===== */
  function initStream(){
    startScr.classList.add('hidden');
    topBar.classList.remove('hidden');
    bottomBar.classList.remove('hidden');

    stream.src = '/stream?' + Date.now();
    streaming = true;

    stream.onload = function(){
      liveDot.classList.add('on');
      liveText.textContent = 'LIVE';
    };

    stream.onerror = function(){
      liveDot.classList.remove('on');
      liveText.textContent = 'OFFLINE';
      if(streaming){
        setTimeout(()=>{ stream.src = '/stream?' + Date.now(); }, 3000);
      }
    };

    startStatusPoll();
    resetHideTimer();
    toast('Streaming dimulai');
  }

  /* ===== STOP STREAM ===== */
  function stopStream(){
    streaming = false;
    stream.src = '';
    liveDot.classList.remove('on');
    liveText.textContent = 'STOPPED';
    fpsVal.textContent = '--';
    clearInterval(statusTimer);
    toast('Streaming dihentikan');
    setTimeout(()=>{
      startScr.classList.remove('hidden');
      topBar.classList.add('hidden');
      bottomBar.classList.add('hidden');
    }, 1000);
  }

  /* ===== FULLSCREEN + LANDSCAPE ===== */
  function goFullscreen(){
    const el = document.documentElement;
    if(!document.fullscreenElement){
      const p = el.requestFullscreen
        ? el.requestFullscreen()
        : el.webkitRequestFullscreen
          ? el.webkitRequestFullscreen()
          : Promise.reject();
      p.then(()=>{
        try{ screen.orientation.lock('landscape').catch(()=>{}) }catch(e){}
        toast('Fullscreen Landscape');
      }).catch(()=> toast('Fullscreen tidak tersedia'));
    } else {
      (document.exitFullscreen || document.webkitExitFullscreen).call(document);
      try{ screen.orientation.unlock(); }catch(e){}
      toast('Keluar Fullscreen');
    }
  }

  /* ===== FIT / FILL ===== */
  function toggleFit(){
    fillMode = !fillMode;
    stream.classList.toggle('fill-mode', fillMode);
    btnFit.querySelector('.label').textContent = fillMode ? 'Fit' : 'Fill';
    btnFit.classList.toggle('active', fillMode);
    toast(fillMode ? 'Mode: Fill' : 'Mode: Fit');
  }

  /* ===== FLASH ===== */
  function toggleFlash(){
    flashOn = !flashOn;
    fetch('/flash?state=' + (flashOn ? '1' : '0'));
    btnFlash.classList.toggle('active', flashOn);
    toast('Flash ' + (flashOn ? 'ON' : 'OFF'));
  }

  /* ===== CAPTURE ===== */
  function capturePhoto(){
    try{
      const c = document.createElement('canvas');
      c.width  = stream.naturalWidth  || 640;
      c.height = stream.naturalHeight || 480;
      c.getContext('2d').drawImage(stream, 0, 0, c.width, c.height);
      const a = document.createElement('a');
      const ts = new Date().toISOString().replace(/[:.]/g,'-');
      a.download = 'ESP32CAM_' + ts + '.jpg';
      a.href = c.toDataURL('image/jpeg', 0.95);
      a.click();
      toast('Foto tersimpan!');
    }catch(e){ toast('Capture gagal'); }
  }

  /* ===== RESOLUTION ===== */
  function changeRes(v){
    fetch('/control?var=framesize&val=' + v)
      .then(()=>{
        const sel = document.getElementById('resSel');
        resLabel.textContent = sel.options[sel.selectedIndex].text;
        toast('Resolusi: ' + sel.options[sel.selectedIndex].text);
      })
      .catch(()=> toast('Gagal ubah resolusi'));
  }

  /* ===== STATUS POLL ===== */
  function startStatusPoll(){
    statusTimer = setInterval(()=>{
      if(!streaming) return;
      fetch('/status')
        .then(r => r.json())
        .then(d =>{
          fpsVal.textContent  = d.fps;
          rssiVal.textContent = d.rssi + 'dBm';
        })
        .catch(()=>{});
    }, 2000);
  }

  /* ===== AUTO-HIDE ===== */
  function showUI(){
    topBar.classList.remove('hidden');
    bottomBar.classList.remove('hidden');
    uiVisible = true;
    resetHideTimer();
  }

  function hideUI(){
    topBar.classList.add('hidden');
    bottomBar.classList.add('hidden');
    uiVisible = false;
  }

  function resetHideTimer(){
    clearTimeout(hideTimer);
    hideTimer = setTimeout(hideUI, 5000);
  }

  stream.addEventListener('click', function(e){
    e.preventDefault();
    uiVisible ? hideUI() : showUI();
  });

  bottomBar.addEventListener('touchstart', resetHideTimer);
  topBar.addEventListener('touchstart', resetHideTimer);
  bottomBar.addEventListener('click', resetHideTimer);
  topBar.addEventListener('click', resetHideTimer);

  /* ===== LOGOUT ===== */
  function confirmLogout(){
    document.getElementById('confirmOverlay').classList.add('show');
  }

  function closeConfirm(){
    document.getElementById('confirmOverlay').classList.remove('show');
  }

  function doLogout(){
    fetch('/logout')
      .then(()=>{
        document.cookie = 'session=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;';
        window.location.href = '/login';
      });
  }

  /* ===== WAKE LOCK ===== */
  let wakeLock = null;
  async function keepScreenOn(){
    try{ wakeLock = await navigator.wakeLock.request('screen'); }catch(e){}
  }

  document.addEventListener('visibilitychange', ()=>{
    if(document.visibilityState === 'visible') keepScreenOn();
  });

  document.addEventListener('contextmenu', e => e.preventDefault());

  window.addEventListener('load', keepScreenOn);
</script>
</body>
</html>
)rawliteral";

// =============================================================
//  HANDLER: HALAMAN UTAMA (cek session)
// =============================================================
void handleRoot() {
  if (!isAuthenticated()) {
    redirectToLogin();
    return;
  }
  server.send_P(200, "text/html", MAIN_PAGE);
}

// =============================================================
//  HANDLER: HALAMAN LOGIN
// =============================================================
void handleLoginPage() {
  // Kalau sudah login, langsung ke dashboard
  if (isAuthenticated()) {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Already logged in");
    return;
  }
  server.send_P(200, "text/html", LOGIN_PAGE);
}

// =============================================================
//  HANDLER: PROSES AUTH (POST /auth)
// =============================================================
void handleAuth() {
  unsigned long now = millis();

  // Cek lockout
  if (loginAttempts >= MAX_LOGIN_ATTEMPTS && now < lockoutUntil) {
    unsigned long remaining = lockoutUntil - now;
    String json = "{\"success\":false,\"locked\":true,\"remaining\":" + String(remaining) + "}";
    server.send(429, "application/json", json);
    Serial.printf("[Auth] Lockout aktif, sisa: %lu ms\n", remaining);
    return;
  }

  // Reset lockout jika sudah lewat
  if (now >= lockoutUntil && loginAttempts >= MAX_LOGIN_ATTEMPTS) {
    loginAttempts = 0;
    Serial.println("[Auth] Lockout selesai, counter direset.");
  }

  String user = server.arg("user");
  String pass = server.arg("pass");

  Serial.printf("[Auth] Percobaan login - User: %s\n", user.c_str());

  if (user == String(CAM_USER) && pass == String(CAM_PASS)) {
    // Login berhasil
    loginAttempts = 0;
    activeSessionToken = generateToken();

    Serial.printf("[Auth] Login BERHASIL! Token: %s\n", activeSessionToken.c_str());

    String json = "{\"success\":true,\"token\":\"" + activeSessionToken + "\"}";
    server.send(200, "application/json", json);
  } else {
    // Login gagal
    loginAttempts++;
    if (loginAttempts >= MAX_LOGIN_ATTEMPTS) {
      lockoutUntil = now + LOCKOUT_DURATION_MS;
      Serial.printf("[Auth] LOCKOUT! %d percobaan gagal.\n", loginAttempts);
    }

    String msg = "Username atau password salah.";
    if (loginAttempts >= MAX_LOGIN_ATTEMPTS) {
      msg = "Akun dikunci sementara. Coba lagi dalam 60 detik.";
    }

    String json = "{\"success\":false,\"locked\":false,\"attempts\":"
                  + String(loginAttempts)
                  + ",\"message\":\"" + msg + "\"}";
    server.send(401, "application/json", json);

    Serial.printf("[Auth] GAGAL! Percobaan ke-%d\n", loginAttempts);
  }
}

// =============================================================
//  HANDLER: LOGOUT
// =============================================================
void handleLogout() {
  Serial.printf("[Auth] Logout - Token dicabut: %s\n", activeSessionToken.c_str());
  activeSessionToken = "";  // Hapus token aktif
  server.sendHeader("Set-Cookie", "session=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/");
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "Logged out");
}

// =============================================================
//  HANDLER: FLASH
// =============================================================
void handleFlash() {
  if (!isAuthenticated()) { redirectToLogin(); return; }
  String state = server.arg("state");
  digitalWrite(FLASH_LED_PIN, state == "1" ? HIGH : LOW);
  server.send(200, "text/plain", state == "1" ? "Flash ON" : "Flash OFF");
}

// =============================================================
//  HANDLER: CONTROL (resolusi, dll)
// =============================================================
void handleControl() {
  if (!isAuthenticated()) { redirectToLogin(); return; }

  String var = server.arg("var");
  int val = server.arg("val").toInt();
  sensor_t *s = esp_camera_sensor_get();

  if (!s) { server.send(500, "text/plain", "Camera error"); return; }

  if (var == "framesize" && val >= 0 && val <= 13) {
    s->set_framesize(s, (framesize_t)val);
    server.send(200, "text/plain", "OK");
    Serial.printf("[Control] Resolusi: %d\n", val);
  } else {
    server.send(400, "text/plain", "Invalid");
  }
}

// =============================================================
//  HANDLER: STATUS
// =============================================================
void handleStatus() {
  if (!isAuthenticated()) { redirectToLogin(); return; }

  String json = "{";
  json += "\"rssi\":"    + String(WiFi.RSSI())          + ",";
  json += "\"fps\":"     + String(currentFPS)           + ",";
  json += "\"ip\":\""   + WiFi.localIP().toString()     + "\",";
  json += "\"uptime\":"  + String(millis() / 1000)      + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap())   + ",";
  json += "\"reconnects\":" + String(wifiReconnectCount)+ ",";
  json += "\"channel\":"  + String(WiFi.channel());
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound() {
  if (!isAuthenticated()) { redirectToLogin(); return; }
  server.send(404, "text/plain", "404 - Not Found");
}

// =============================================================
//  SETUP
// =============================================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.println("\n\n");
  Serial.println("==========================================");
  Serial.println("  ESP32-CAM Fullscreen + Login Server");
  Serial.println("==========================================");
  Serial.printf("  User: %s\n", CAM_USER);
  Serial.println("==========================================\n");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  setupWiFi();

  if (!setupCamera()) {
    Serial.println("[System] Kamera gagal, restart...");
    delay(3000);
    ESP.restart();
  }

  // Kumpulkan header Cookie untuk autentikasi
  const char* headers[] = {"Cookie"};
  server.collectHeaders(headers, 1);

  // Routes
  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/login",   HTTP_GET,  handleLoginPage);
  server.on("/auth",    HTTP_POST, handleAuth);
  server.on("/logout",  HTTP_GET,  handleLogout);
  server.on("/stream",  HTTP_GET,  handleStream);
  server.on("/flash",   HTTP_GET,  handleFlash);
  server.on("/control", HTTP_GET,  handleControl);
  server.on("/status",  HTTP_GET,  handleStatus);
  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println("[Server] Web Server aktif!");
  Serial.printf("[Server] Buka: http://%s/\n", WiFi.localIP().toString().c_str());
}

// =============================================================
//  LOOP
// =============================================================
void loop() {
  server.handleClient();
  checkWiFiConnection();
}
