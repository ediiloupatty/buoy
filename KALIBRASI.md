# 🔬 Panduan Kalibrasi Sensor pH — Smart Buoy IoT System

Dokumen ini menjelaskan prosedur kalibrasi **Two-Point Linear Calibration** untuk sensor pH analog pada sistem Smart Buoy. Kalibrasi dilakukan melalui Serial Monitor dan hasilnya tersimpan permanen di memori flash ESP32 (NVS).

---

## 1. Alat dan Bahan yang Dibutuhkan

| No | Item | Keterangan |
|----|------|------------|
| 1 | **Larutan Buffer pH 4.01** | Larutan standar asam (warna merah/kuning) |
| 2 | **Larutan Buffer pH 6.86** | Larutan standar netral (warna hijau/biru) |
| 3 | **Aquades (Air Suling)** | Untuk membilas sensor antara pengukuran |
| 4 | **Gelas Beaker / Wadah Bersih** | Minimal 3 buah (masing-masing untuk buffer dan bilas) |
| 5 | **Tisu / Kain Lembut** | Untuk mengeringkan probe sebelum pindah larutan |
| 6 | **Laptop + Kabel USB** | Untuk koneksi Serial Monitor ke ESP32 |
| 7 | **Smart Buoy (ESP32 + Sensor pH)** | Perangkat yang akan dikalibrasi |

> **Catatan:** Larutan buffer tersedia di toko kimia / laboratorium. Pastikan larutan belum kedaluwarsa dan disimpan pada suhu ruangan (25°C ± 2°C).

---

## 2. Persiapan Sebelum Kalibrasi

### 2.1 Persiapan Perangkat Keras
1. Pastikan sensor pH terpasang dengan benar pada GPIO **34** (ADC1).
2. Pastikan sensor suhu DS18B20 terpasang pada GPIO **4**.
3. Hubungkan ESP32 ke laptop via kabel USB.

### 2.2 Persiapan Software
1. Upload firmware terbaru ke ESP32 (jika belum).
2. Buka **Serial Monitor** di Arduino IDE:
   - Baud Rate: **115200**
   - Line Ending: **Newline** (penting! harus "Newline" atau "Both NL & CR")
3. Tunggu hingga ESP32 terhubung ke WiFi dan menampilkan status kalibrasi:
   ```
   ═══════════════════════════════════════
     pH Calibration Loaded from NVS
     Slope (m) = -4.7900
     Intercept (b) = 18.0600
   ═══════════════════════════════════════
   ```

### 2.3 Persiapan Larutan
1. Tuangkan larutan **buffer pH 4.01** ke gelas beaker pertama.
2. Tuangkan larutan **buffer pH 6.86** ke gelas beaker kedua.
3. Siapkan gelas beaker ketiga berisi **aquades** untuk membilas.
4. Biarkan semua larutan berada pada **suhu ruangan** (±25°C) selama minimal 10 menit.

---

## 3. Prosedur Kalibrasi (Step by Step)

### 📋 Step 1 — Cek Status Kalibrasi Saat Ini

Ketik perintah berikut di Serial Monitor:

```
CALINFO
```

Output yang diharapkan:
```
═══════════════════════════════════════
  STATUS KALIBRASI pH
═══════════════════════════════════════
  Slope (m)     = -4.7900
  Intercept (b) = 18.0600
  Sesi aktif    : CAL4=—, CAL7=—
═══════════════════════════════════════
  Perintah: CAL4 | CAL7 | CALSAVE | CALINFO
═══════════════════════════════════════
```

> Ini menunjukkan nilai kalibrasi yang sedang digunakan. Jika ini kalibrasi pertama, nilainya adalah default bawaan firmware.

---

### 📋 Step 2 — Kalibrasi Titik pH 4.01 (Buffer Asam)

1. **Bilas** probe sensor pH dengan aquades.
2. **Keringkan** permukaan probe dengan tisu secara lembut (jangan digosok keras).
3. **Celupkan** probe ke dalam larutan **buffer pH 4.01**.
4. **Tunggu 2-3 menit** hingga pembacaan stabil. Amati output Serial Monitor:
   ```
   [Sensor] Suhu=25.3°C | pH=3.87 | Turb=1230 (Jernih)
   [Sensor] Suhu=25.2°C | pH=3.89 | Turb=1228 (Jernih)
   [Sensor] Suhu=25.3°C | pH=3.88 | Turb=1231 (Jernih)  ← stabil
   ```
   > Nilai pH belum akurat — itu normal, karena belum dikalibrasi.

5. Setelah stabil, ketik di Serial Monitor:
   ```
   CAL4
   ```

6. Output yang diharapkan:
   ```
   ╔══════════════════════════════════════╗
   ║   KALIBRASI pH 4.01 — Sampling...    ║
   ╚══════════════════════════════════════╝
     ✓ Tegangan tercatat: 2.9312 V
     ✓ ADC ~ 3634
     → Lanjutkan: celupkan ke buffer pH 6.86, lalu ketik CAL7
   ```

7. **Catat** nilai tegangan yang muncul di tabel berikut:

   | Parameter | Nilai |
   |-----------|-------|
   | Tegangan pH 4.01 | ........ V |
   | ADC pH 4.01 | ........ |

---

### 📋 Step 3 — Bilas Sensor

1. **Angkat** probe dari larutan buffer pH 4.01.
2. **Celupkan** probe ke dalam aquades selama 10-15 detik sambil digoyang perlahan.
3. **Keringkan** permukaan probe dengan tisu.

> ⚠️ **PENTING:** Langkah bilas ini wajib dilakukan untuk menghindari kontaminasi silang antara larutan buffer. Kontaminasi dapat menyebabkan kalibrasi tidak akurat.

---

### 📋 Step 4 — Kalibrasi Titik pH 6.86 (Buffer Netral)

1. **Celupkan** probe ke dalam larutan **buffer pH 6.86**.
2. **Tunggu 2-3 menit** hingga pembacaan stabil.
3. Setelah stabil, ketik di Serial Monitor:
   ```
   CAL7
   ```

4. Output yang diharapkan:
   ```
   ╔══════════════════════════════════════╗
   ║   KALIBRASI pH 6.86 — Sampling...    ║
   ╚══════════════════════════════════════╝
     ✓ Tegangan tercatat: 2.3350 V
     ✓ ADC ~ 2896
     → Lanjutkan: ketik CALSAVE untuk menghitung & menyimpan kalibrasi
   ```

5. **Catat** nilai tegangan:

   | Parameter | Nilai |
   |-----------|-------|
   | Tegangan pH 6.86 | ........ V |
   | ADC pH 6.86 | ........ |

---

### 📋 Step 5 — Simpan Kalibrasi

Ketik perintah berikut untuk menghitung dan menyimpan nilai kalibrasi:

```
CALSAVE
```

Output yang diharapkan:
```
╔══════════════════════════════════════╗
║    ✓ KALIBRASI BERHASIL DISIMPAN     ║
╠══════════════════════════════════════╣
║  V(pH4) = 2.9312 V                  ║
║  V(pH7) = 2.3350 V                  ║
║  Slope (m)     = -4.7819            ║
║  Intercept (b) = 18.0216            ║
╠══════════════════════════════════════╣
║  Data tersimpan di NVS Flash.        ║
║  Restart aman — nilai tidak hilang.  ║
╚══════════════════════════════════════╝
```

> Sistem akan menghitung **slope (m)** dan **intercept (b)** menggunakan rumus:
> ```
> m = (pH₂ - pH₁) / (V₂ - V₁) = (6.86 - 4.01) / (V_pH7 - V_pH4)
> b = pH₁ - (m × V_pH4)        = 4.01 - (m × V_pH4)
> ```

---

### 📋 Step 6 — Verifikasi Kalibrasi

1. **Bilas** probe dengan aquades dan keringkan.
2. **Celupkan kembali** probe ke larutan buffer pH 6.86 (atau pH 4.01).
3. Amati pembacaan di Serial Monitor:
   ```
   [Sensor] Suhu=25.1°C | pH=6.84 | Turb=1225 (Jernih)
   ```
4. **Hitung persentase error:**
   ```
   % Error = |pH_terbaca - pH_sebenarnya| / pH_sebenarnya × 100%

   Contoh: |6.84 - 6.86| / 6.86 × 100% = 0.29%
   ```

5. **Catat** hasil validasi:

   | Parameter | Nilai |
   |-----------|-------|
   | pH Buffer Standar | 6.86 |
   | pH Terbaca Sensor | ........ |
   | Selisih | ........ |
   | % Error | ........% |

> ✅ Kalibrasi dianggap **berhasil** jika % error < **2%**.
> ⚠️ Jika error > 5%, ulangi proses kalibrasi dari Step 2.

---

### 📋 Step 7 — Uji Persistensi (Opsional)

Untuk membuktikan bahwa kalibrasi tersimpan permanen:

1. **Cabut** kabel USB ESP32 (matikan total).
2. **Tunggu** 10 detik.
3. **Hubungkan kembali** kabel USB.
4. Buka Serial Monitor dan perhatikan pesan boot:
   ```
   ═══════════════════════════════════════
     pH Calibration Loaded from NVS
     Slope (m) = -4.7819
     Intercept (b) = 18.0216
   ═══════════════════════════════════════
   ```

> Jika nilai **m** dan **b** sama dengan yang ditampilkan saat CALSAVE, maka NVS persistence berfungsi dengan benar. ✅

---

## 4. Dasar Teori Kalibrasi

### 4.1 Prinsip Kerja Sensor pH

Sensor pH analog menghasilkan **tegangan listrik** yang berbanding lurus (linear) dengan tingkat keasaman larutan. Hubungan antara tegangan dan pH mengikuti persamaan linear:

```
pH = (m × V) + b
```

Dimana:
- `V` = Tegangan output sensor (Volt)
- `m` = Slope / kemiringan garis kalibrasi
- `b` = Intercept / perpotongan sumbu Y

### 4.2 Metode Two-Point Linear Calibration

Kalibrasi 2 titik menggunakan dua larutan buffer dengan pH yang diketahui untuk menentukan persamaan garis linear:

```
    pH
    ▲
 9  │
    │
 7  │ ─ ─ ─ ─ ─ ─ ● (V₂, pH 6.86)
    │            ╱
 6  │          ╱    pH = m × V + b
    │        ╱
 5  │      ╱
    │    ╱
 4  │ ● (V₁, pH 4.01)
    │
    └────────────────────► Voltage (V)
       1.0   1.5   2.0   2.5   3.0
```

### 4.3 Kompensasi Suhu (Nernst)

Elektroda pH sensitif terhadap suhu. Pada suhu selain 25°C, terjadi penyimpangan pembacaan. Sistem ini menggunakan koreksi Nernst sederhana:

```
pH_terkoreksi = pH_mentah + 0.003 × (Suhu - 25.0)
```

Data suhu diambil secara real-time dari sensor DS18B20 yang terintegrasi pada sistem.

### 4.4 Trimmed Mean Filter

Untuk mengurangi noise pada pembacaan ADC, sistem menggunakan metode **Trimmed Mean**:

1. Ambil **10 sampel** ADC berturut-turut (interval 10ms)
2. **Urutkan** dari kecil ke besar
3. **Buang** 2 nilai terkecil dan 2 nilai terbesar (outlier)
4. **Rata-ratakan** 6 sampel sisanya
5. Konversi ke tegangan: `V = ADC × (3.3 / 4095)`

```
Contoh:
  Sampel mentah : [2890, 2905, 2856, 2912, 2895, 2988, 2901, 2898, 2843, 2910]
  Setelah sort  : [2843, 2856, 2890, 2895, 2898, 2901, 2905, 2910, 2912, 2988]
  Buang outlier : [────, ────, 2890, 2895, 2898, 2901, 2905, 2910, ────, ────]
  Rata-rata     : (2890 + 2895 + 2898 + 2901 + 2905 + 2910) / 6 = 2899.8
  Tegangan      : 2899.8 × (3.3 / 4095) = 2.336 V
```

---

## 5. Troubleshooting

| Masalah | Penyebab | Solusi |
|---------|----------|-------|
| Error: "Kedua titik kalibrasi harus diambil" | CAL4 atau CAL7 belum dijalankan | Jalankan CAL4 dan CAL7 sebelum CALSAVE |
| Error: "Tegangan kedua buffer terlalu mirip" | Sensor tidak tercelup dengan benar / probe rusak | Cek koneksi sensor, pastikan tercelup penuh |
| pH terbaca jauh dari nilai sebenarnya | Larutan buffer kedaluwarsa / terkontaminasi | Ganti larutan buffer baru |
| Perintah serial tidak direspon | Line ending di Serial Monitor salah | Ubah ke "Newline" atau "Both NL & CR" |
| Nilai kalibrasi hilang setelah restart | NVS flash bermasalah | Cek apakah CALSAVE berhasil (lihat pesan konfirmasi) |
| Pembacaan pH berfluktuasi tinggi | Noise listrik / grounding buruk | Tambahkan kapasitor 100nF pada pin sensor |

---

## 6. Perintah Serial Lengkap

| Perintah | Fungsi |
|----------|--------|
| `CAL4` | Catat tegangan sensor pada larutan buffer pH 4.01 |
| `CAL7` | Catat tegangan sensor pada larutan buffer pH 6.86 |
| `CALSAVE` | Hitung slope & intercept, simpan ke NVS flash |
| `CALINFO` | Tampilkan status kalibrasi saat ini |

---

## 7. Jadwal Rekalibrasi

| Kondisi | Rekomendasi |
|---------|-------------|
| Penggunaan normal | Setiap **1-2 bulan** sekali |
| Sensor baru dipasang | **Wajib** kalibrasi sebelum digunakan |
| Setelah perawatan / pembersihan probe | **Wajib** kalibrasi ulang |
| Pembacaan terasa tidak akurat | Segera kalibrasi ulang |

---

*Dokumen ini adalah bagian dari dokumentasi Smart Buoy IoT System.*
*Terakhir diperbarui: Mei 2026*
