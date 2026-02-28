# 📹 ESP32-CAM Streaming Server

![Platform](https://img.shields.io/badge/Platform-ESP32--CAM-blue)
![License](https://img.shields.io/badge/License-MIT-green)
![Mode](https://img.shields.io/badge/Mode-AP%20%7C%20Router-orange)

ESP32-CAM MJPEG Streaming Server dengan tampilan **Fullscreen Landscape**, **Login Access**, dan dua mode koneksi: **Router Mode** dan **Access Point (AP) Mode**.

---

## ✨ Fitur Utama

| Fitur | Keterangan |
|-------|------------|
| 🔐 **Login Access** | Halaman login dengan session token sebelum akses stream |
| 🛡️ **Brute-Force Protection** | Lockout 60 detik setelah 5x percobaan login salah |
| 📱 **Fullscreen Landscape** | Tampilan penuh layar, optimal untuk monitoring via smartphone |
| 🎥 **MJPEG Streaming** | Stream video real-time langsung di browser |
| 📐 **Fit / Fill Mode** | Toggle antara tampilan proporsional dan penuh layar |
| 📷 **Capture / Snapshot** | Ambil foto dari stream, otomatis tersimpan ke galeri |
| 💡 **Flash Control** | Nyalakan/matikan LED flash dari browser |
| 📊 **Live Status** | FPS, RSSI/Jumlah klien ditampilkan real-time |
| 🔄 **Dynamic Resolution** | Ubah resolusi kamera langsung dari browser |
| ☀️ **Wake Lock** | Layar smartphone tidak mati saat monitoring |
| 🔁 **Auto Reconnect** | (Router Mode) Otomatis reconnect jika WiFi terputus |

---

## 📁 Struktur Repository

```
esp32-cam-streaming-server/
├── firmware/
│   ├── ESP32CAM_Router_Mode/
│   │   └── ESP32CAM_Router_Mode.ino   ← Mode via Router WiFi
│   └── ESP32CAM_AP_Mode/
│       └── ESP32CAM_AP_Mode.ino       ← Mode Hotspot sendiri
├── docs/
│   └── wiring-diagram.md              ← Panduan pin & wiring
├── LICENSE
└── README.md
```

---

## 🔌 Perbandingan Mode

| | Router Mode | AP Mode |
|--|-------------|---------|
| **Perlu Router** | ✅ Ya | ❌ Tidak |
| **Perlu Internet** | ❌ Tidak | ❌ Tidak |
| **HP Bisa Internetan** | ✅ Ya | ❌ Tidak |
| **IP Akses** | `192.168.1.100` | `192.168.4.1` |
| **Max Klien** | Sesuai router | 4 perangkat |
| **Cocok Untuk** | Monitoring rumah/kantor | Lokasi tanpa infrastruktur |

---

## ⚙️ Persyaratan

- **Hardware:** ESP32-CAM AI-Thinker + FTDI Programmer
- **Arduino IDE:** Versi 1.8.x atau 2.x
- **Board Package:** `esp32` by Espressif Systems v2.x
- **Library:** `esp_camera` (sudah built-in di board package ESP32)

---

## 🚀 Cara Upload

### 1. Install Board ESP32
Buka Arduino IDE → **File → Preferences** → tambahkan URL berikut di *Additional Boards Manager URLs*:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
Lalu **Tools → Board → Boards Manager** → cari `esp32` → Install.

### 2. Pilih Board
**Tools → Board → ESP32 Arduino → AI Thinker ESP32-CAM**

### 3. Konfigurasi Upload
| Setting | Value |
|---------|-------|
| Board | AI Thinker ESP32-CAM |
| Port | COM port FTDI Anda |
| Upload Speed | 115200 |

### 4. Mode Upload (FTDI)
Hubungkan pin **IO0 → GND** sebelum upload, lepas setelah selesai.

---

## 📲 Cara Penggunaan

### Router Mode
```
1. Edit ssid & password di kode
2. Upload ke ESP32-CAM
3. Buka browser → http://192.168.1.100/
4. Login → Streaming!
```

### AP Mode
```
1. Upload ke ESP32-CAM
2. HP → WiFi → Hubungkan ke "ESP32-CAM"
3. Buka browser → http://192.168.4.1/
4. Login → Streaming!
```

---

## 🔐 Kredensial Default

### Router Mode
| | Value |
|-|-------|
| **Username** | `admin` |
| **Password** | `esp32cam` |

### AP Mode
| | Value |
|-|-------|
| **WiFi SSID** | `ESP32-CAM` |
| **WiFi Password** | `esp32cam123` |
| **Username** | `admin` |
| **Password** | `admin123` |

> ⚠️ **Ganti kredensial default sebelum digunakan!**

---

## 🔧 Konfigurasi Cepat

### Router Mode — sesuaikan bagian ini:
```cpp
const char* ssid     = "NAMA_WIFI_ANDA";
const char* password = "PASSWORD_WIFI_ANDA";
const char* CAM_USER = "admin";
const char* CAM_PASS = "esp32cam";

IPAddress local_IP(192, 168, 1, 100);  // IP statis ESP32-CAM
IPAddress gateway(192, 168, 1, 1);
```

### AP Mode — sesuaikan bagian ini:
```cpp
const char* AP_SSID     = "ESP32-CAM";
const char* AP_PASSWORD = "esp32cam123";
const char* CAM_USER    = "admin";
const char* CAM_PASS    = "admin123";
```

---

## 🗺️ Endpoint API

| Endpoint | Method | Deskripsi |
|----------|--------|-----------|
| `/` | GET | Halaman dashboard (perlu login) |
| `/login` | GET | Halaman login |
| `/auth` | POST | Proses autentikasi |
| `/logout` | GET | Logout & hapus session |
| `/stream` | GET | MJPEG stream (perlu login) |
| `/flash?state=1` | GET | Nyalakan flash LED |
| `/flash?state=0` | GET | Matikan flash LED |
| `/control?var=framesize&val=8` | GET | Ubah resolusi |
| `/status` | GET | Status JSON (FPS, RSSI, dll) |

---

## 📐 Tabel Resolusi

| Value | Nama | Resolusi |
|-------|------|----------|
| 13 | UXGA | 1600×1200 |
| 11 | HD | 1280×720 |
| 10 | XGA | 1024×768 |
| 9 | SVGA | 800×600 |
| 8 | VGA | 640×480 ✅ Rekomendasi |
| 5 | QVGA | 320×240 |

---

## 🐛 Troubleshooting

| Masalah | Solusi |
|---------|--------|
| Kamera gagal init | Periksa koneksi kabel, pastikan power supply stabil 5V/2A |
| Tidak bisa upload | Pastikan IO0 terhubung ke GND saat upload |
| Stream patah-patah | Turunkan resolusi atau naikkan JPEG quality |
| IP tidak bisa diakses | Pastikan HP & ESP32 di jaringan yang sama |
| Login tidak bisa | Periksa username/password di kode, restart ESP32-CAM |

---

## 📄 License

MIT License — bebas digunakan, dimodifikasi, dan didistribusikan.

---

## 🙏 Credits

Dibuat dengan ❤️ menggunakan ESP32-CAM AI-Thinker dan Arduino Framework.
