# 🔌 Wiring Diagram & Pin Reference

## ESP32-CAM AI-Thinker Pin Map

```
┌─────────────────────────────┐
│        ESP32-CAM            │
│                             │
│  5V  ────────── VCC (5V)    │
│  GND ────────── GND         │
│  U0R ────────── TX (FTDI)   │
│  U0T ────────── RX (FTDI)   │
│  IO0 ────────── GND (upload)│
│                             │
│  GPIO 4 ─── Flash LED       │
└─────────────────────────────┘
```

---

## 🔧 Koneksi FTDI untuk Upload

| ESP32-CAM | FTDI Programmer |
|-----------|-----------------|
| 5V | VCC (set ke 5V) |
| GND | GND |
| U0R (RX) | TX |
| U0T (TX) | RX |
| IO0 | GND (saat upload saja) |

> ⚠️ **PENTING:** IO0 → GND hanya saat proses upload. Lepas setelah upload selesai, lalu tekan tombol Reset.

---

## ⚡ Rekomendasi Power Supply

| Spesifikasi | Nilai |
|-------------|-------|
| Tegangan | 5V DC |
| Arus minimal | 2A |
| Konektor | Micro USB / Pin langsung |

> ⚠️ Power supply lemah (< 500mA) menyebabkan kamera gagal init atau brownout reset.

---

## 📐 Tabel Resolusi & Performa

| Resolusi | Ukuran | FPS (estimasi) | Rekomendasi |
|----------|--------|----------------|-------------|
| UXGA 1600×1200 | Besar | 5-8 fps | Foto statik |
| HD 1280×720 | Besar | 8-12 fps | Kualitas tinggi |
| VGA 640×480 | Sedang | 15-25 fps | ✅ Streaming optimal |
| QVGA 320×240 | Kecil | 25-30 fps | Bandwidth rendah |

---

## 🔩 Kamera Pin Internal (Referensi)

| Define | GPIO | Fungsi |
|--------|------|--------|
| PWDN_GPIO_NUM | 32 | Power Down |
| XCLK_GPIO_NUM | 0 | Clock |
| SIOD_GPIO_NUM | 26 | I2C SDA |
| SIOC_GPIO_NUM | 27 | I2C SCL |
| VSYNC_GPIO_NUM | 25 | V-Sync |
| HREF_GPIO_NUM | 23 | H-Ref |
| PCLK_GPIO_NUM | 22 | Pixel Clock |
| FLASH_LED_PIN | 4 | Flash LED |
