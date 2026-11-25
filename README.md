# Pharma Medic

> **Smart Pharmaceutical & Vaccine Cold Storage IoT Monitoring**  
> Sistem pemantauan telemetri rantai dingin (*cold chain* 2–8 °C) dan sterilisasi ruang UV-C gudang farmasi berbasis Internet of Things (IoT).

[![Release](https://img.shields.io/github/v/release/ryyv3nn/Pharma?style=flat-square&color=0E555A)](https://github.com/ryyv3nn/Pharma/releases/latest)
[![Flutter](https://img.shields.io/badge/Flutter-3.x-02569B?style=flat-square&logo=flutter)](https://flutter.dev)
[![FastAPI](https://img.shields.io/badge/FastAPI-Python-009688?style=flat-square&logo=fastapi)](https://fastapi.tiangolo.com)
[![License](https://img.shields.io/badge/License-MIT-green.svg?style=flat-square)](LICENSE)

---

## Ringkasan Proyek

**Pharma Medic** memantau kestabilan penyimpanan vaksin (*cold storage* `2–8 °C`, kelembapan `35–60 %`, kadar gas `< 50 ppm`, pencahayaan `< 300 Lux`) serta keamanan ruang sterilisasi UV-C (*cleanroom* `15–25 °C`). Data dikirim dari ESP32-S3 melalui **MQTT (Mosquitto)** menuju **FastAPI + MySQL** di Cloud VPS, lalu dipantau secara *real-time* dari **Aplikasi Android (Flutter)** dan **Web Admin Dashboard**.

---

## Fitur Utama

- **Real-time Telemetry & Alarm**: Gauge suhu dan kelembapan, deteksi asap/gas (MQ-2), intensitas cahaya (LDR), status pintu (*reed switch*), gerak manusia (PIR), serta kontrol aktuator dua arah (Kipas, Sirine Buzzer + Mute, Pompa Air, Lampu UV-C dengan *Safety Interlock*, LED RGB).
- **Multi-Node Switching**: Pemantauan terpisah untuk **Device 1** (Rak Vaksin / Cold Storage) dan **Device 2** (Ruang Steril & Keamanan Pintu).
- **Digital Twin 3D & Denah 2D**: Model 3D gudang interaktif (`model_3d_v2.glb`) dengan fokus kamera per zona serta tampilan Denah 2D.
- **Web Admin Dashboard**: Pemantauan cluster 4-Node Docker, statistik agregat, kartu live in-memory (`/sensor/latest`), manajemen staf/operator, simulator perangkat, API Tester, serta filter dan export CSV.
- **Riwayat Log & Ekspor CSV**: Penyimpanan otomatis dengan *smart throttling* database (1 log/menit saat stabil, langsung catat saat bahaya/perubahan status) dan export `.csv` dari Mobile maupun Web.
- **Autentikasi & OTA Update**: Login Operator/User (termasuk dukungan biometrik sidik jari) dan pembaruan APK otomatis melalui GitHub Releases.

---

## Tech Stack

| Komponen | Teknologi |
|---|---|
| **Mobile App** | Flutter (Dart ^3.13), `provider`, `mqtt_client`, `syncfusion_flutter_gauges`, `fl_chart`, `model_viewer_plus`, `flutter_secure_storage` |
| **Backend & Web** | FastAPI (Python), MySQL 8.0, Eclipse Mosquitto MQTT (`:1883` TCP & `:9001` WSS), Caddy SSL, HTML/JS Dashboard |
| **Hardware (IoT)** | ESP32-S3 (`DS18B20`, `DHT22`, `MQ-2`, `LDR`, `PIR`, `Reed Switch`, `HC-SR04`, Relay Kipas/Pompa/UV-C, Buzzer, LED RGB) |
| **Cloud / Infra** | Docker-in-Docker 4-Node (Node 1 FastAPI + Caddy, Node 2 MySQL, Node 3 Mosquitto, Node 4 Auto-Backup Watcher) |

---

## Struktur Proyek

```text
Pharma/
├── assets/
│   ├── images/                     # Denah ruang vaksin & aset gambar
│   └── models/                     # Model 3D Digital Twin (.glb)
├── backend/
│   ├── .env.example                # Contoh konfigurasi environment backend
│   ├── dashboard.html              # Web Admin Panel & Live Dashboard
│   └── main.py                     # REST API FastAPI & MQTT-MySQL bridge
├── docs/
│   └── System Design & Flowchart/  # Diagram arsitektur, activity & flowchart (.drawio/.png)
├── hardware/
│   ├── physical/
│   │   └── device1_physical.ino    # Firmware fisik ESP32-S3 Node 1 (DS18B20, DHT22, MQ-2, LDR)
│   └── wokwi/
│       ├── device1_sketch.ino      # Simulasi Wokwi Node 1
│       ├── device2_sketch.ino      # Simulasi Wokwi Node 2 (PIR, Reed Switch, HC-SR04, UV-C)
│       └── wokwi_diagram.json      # Skema rangkaian Wokwi Node 2
├── lib/                            # Source code aplikasi Flutter
│   ├── models/                     # Model data sensor & normalisasi payload
│   ├── providers/                  # State management (MQTT + REST + thresholds + history)
│   ├── services/                   # Klien HTTP, MQTT, storage lokal, OTA updater
│   ├── theme/                      # Token warna & tipografi
│   ├── views/                      # Layar dashboard, realtime, riwayat, 3D, pengaturan
│   └── widgets/                    # Grafik, toast, shimmer, dekorasi UI
├── scripts/
│   └── simulator.py                # Simulator telemetri MQTT dua node
├── test/                           # Unit & widget tests Flutter
└── pubspec.yaml                    # Manifest & dependensi Flutter
```

Alur data singkat:

```text
ESP32 / Simulator ──MQTT (:1883)──▶ Mosquitto ──▶ FastAPI ──(60s throttle)──▶ MySQL 8.0
        │                               │             └─(RAM /sensor/latest)──▶ Web Dashboard
        └──────────MQTT stream──────────┴──▶ Flutter App ◀────REST HTTPS───────┘
                                                │
                              perintah aktuator ┴──▶ pharma/control/...
```

---

## Panduan Instalasi & Menjalankan

### 1. Aplikasi Mobile (Flutter)
```bash
git clone https://github.com/ryyv3nn/Pharma.git
cd Pharma
flutter pub get
flutter run
```
Aplikasi secara bawaan terhubung ke server cloud (`api.pharma.web.id`). Untuk pengujian ke backend lokal via USB:
```bash
adb reverse tcp:8001 tcp:8001
adb reverse tcp:1883 tcp:1883
```

### 2. Backend FastAPI
Salin `backend/.env.example` menjadi `backend/.env` (atau set environment variable `DB_HOST`, `DB_USER`, `DB_PASS`, `DB_NAME`, `MQTT_HOST`, `PHARMA_SECRET_KEY`), lalu jalankan:
```bash
uvicorn backend.main:app --host 0.0.0.0 --port 8001
```

### 3. Simulator Perangkat (Opsional)
```bash
uv run --with paho-mqtt python scripts/simulator.py
```

---

## Pengembang

- **Pengembang**: Fabian Hendriyansah
- **Repositori**: [https://github.com/ryyv3nn/Pharma](https://github.com/ryyv3nn/Pharma)
