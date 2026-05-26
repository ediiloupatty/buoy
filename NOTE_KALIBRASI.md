# Catatan Kalibrasi Turbidity Sensor (3-Titik Piecewise)

> Versi: 3-titik (0 NTU, 70 NTU, 400 NTU)
> Metode: Piecewise linear — 1 garis untuk 0–70 NTU, 1 garis lagi untuk 70–400 NTU.
> Lebih akurat dari 2-titik di range rendah (penting buat tambak udang sehat 30–80 NTU).

## Bahan yang Harus Disiapkan

### Wadah A — Air Jernih (titik 0 NTU)
- Air galon / air mineral
- **Saring 2x pakai tisu** sebelum dipakai
- Wadah bening, minimal 250 ml
- Pastikan tidak ada serat tisu yang lolos (kalau ada, diamkan 5 menit, ambil bagian atas)

### Wadah B — Air Susu Encer (titik ~70 NTU)
- 600 ml air mineral
- **±1.5 ml susu Indomilk UHT plain** (≈ 1/3 tutup botol aqua, atau ¼ sendok teh)
- Tips: pakai sendok kecil teteskan 6–8 tetes susu, aduk rata
- Kocok kuat 5 detik sebelum dipakai
- Secara visual: agak keruh tapi dasar gelas masih kelihatan jelas

### Wadah C — Air Susu Pekat (titik ~400 NTU)
- 600 ml air mineral
- **1 tutup botol aqua susu (~5 ml)** Indomilk UHT plain
- Kocok kuat 10 detik sebelum dipakai
- Secara visual: putih susu, dasar gelas tidak terlihat

### Wadah D — Air Tambak (validasi)
- Air langsung dari tambak
- Ambil di kedalaman ~30 cm (bukan permukaan, bukan dasar)
- Tuang ke wadah bening yang cukup dalam

### Peralatan
- ESP32 sudah ter-upload sketch `kalibrasi/kalibrasi.ino`
- Kabel USB
- Laptop dengan Arduino IDE (Serial Monitor)
- Tisu microfiber + alkohol/spiritus (lap sensor antar wadah)
- Sendok kecil bersih untuk aduk

## Persiapan Sensor (Penting!)

1. **Lap optical surface** sensor dengan microfiber + alkohol
2. **Pastikan PCB hitam kering**, taruh jauh dari area air
3. **Cek kabel VCC** terhubung ke pin **5V/VIN** ESP32 (bukan 3V3)
4. **Buka Serial Monitor**: baudrate 115200, mode "Newline" atau "Both NL & CR"

## Step Kalibrasi (3-Titik)

### Step 1: Kalibrasi 0 NTU (Wadah A)
```
1. Celup sensor ke Wadah A (air filter)
2. Goyangkan pelan untuk usir gelembung di celah tanduk
3. Tunggu 15-20 detik sampai voltase stabil
4. Ketik di Serial Monitor: TCAL0
5. Tekan Enter
6. Catat voltase V0 yang muncul: ________ V
```

### Step 2: Kalibrasi 70 NTU (Wadah B)
```
1. Angkat sensor dari Wadah A, bilas dengan air filter, lap optical surface
2. Kocok kuat Wadah B (susu encer) 5 detik
3. Celup sensor ke Wadah B
4. Tunggu 15-20 detik sampai stabil
5. Ketik: TCAL70
6. Tekan Enter
7. Catat voltase V70 yang muncul: ________ V
```

### Step 3: Kalibrasi 400 NTU (Wadah C)
```
1. Angkat sensor dari Wadah B, bilas dengan air filter, lap optical surface
2. Kocok kuat Wadah C (susu pekat) 10 detik
3. Celup sensor ke Wadah C
4. Tunggu 15-20 detik sampai stabil
5. Ketik: TCAL400
6. Tekan Enter
7. Catat voltase V400 yang muncul: ________ V
```

### Step 4: Simpan ke NVS
```
1. Pastikan V0 > V70 > V400 (turun monoton) — kalau tidak monoton, ulang!
2. Ketik: TCALSAVE
3. Tekan Enter
4. Muncul konfirmasi "KALIBRASI TURBIDITY 3-TITIK SAVED"
```

## Cek Ekspektasi Voltase

| Titik | Voltase Normal (sensor sehat) | Voltase Sensor Kamu (modul pernah kemasukan air) |
|-------|-------------------------------|--------------------------------------------------|
| V0 (air jernih)   | 2.5 – 3.2 V | ~1.46 – 1.70 V |
| V70 (susu encer)  | 1.5 – 2.5 V | ~1.20 – 1.35 V |
| V400 (susu pekat) | 1.0 – 2.0 V | ~1.00 – 1.10 V |
| Selisih V0 – V400 | min. 0.5 V, ideal 1.0+ V | ≥ 0.40 V |
| Selisih V0 – V70  | min. 0.05 V | ≥ 0.08 V |
| Selisih V70 – V400 | min. 0.05 V | ≥ 0.08 V |

### Aturan Wajib
**V0 > V70 > V400** dengan selisih antar titik minimal **0.02 V**. Kalau tidak, `TCALSAVE` akan menolak menyimpan.

### Troubleshooting Cepat
- **V70 ≈ V0** (selisih <0.05V) → susu Wadah B kurang, tambah 3–5 tetes susu, ulang TCAL70
- **V70 ≈ V400** (selisih <0.05V) → susu Wadah B kelewat pekat, tambah 50–100 ml air, ulang TCAL70
- **V400 = 0.0 – 0.2 V (saturasi)** → susu Wadah C terlalu pekat, tambah 100–200 ml air mineral, ulang TCAL400
- **V0 < V70 atau V70 < V400 (urutan terbalik)** → wadah ketukar atau sensor bermasalah; cek ulang dan ulangi TCAL_ yang salah
- **Selisih sangat kecil semua** → modul sensor degraded (kemasukan air); lap optical surface + keringkan PCB sebelum lanjut

## Step 5: Validasi Pakai Air Tambak

Setelah TCALSAVE selesai:

```
1. Bilas sensor dengan air mineral
2. Lap pelan optical surface
3. Celup ke Wadah C (air tambak)
4. Tunggu 15-20 detik stabil
5. Ketik: CALINFO (untuk lihat status terakhir)
   ATAU
   Lihat reading NTU di Serial Monitor (kalau sketch print continuous)
6. Catat hasil: ________ NTU
```

### Hasil Validasi yang Wajar (Tambak Udang)
- Tambak sehat normal: **30 - 80 NTU**
- Tambak agak keruh: **80 - 150 NTU**
- Tambak sangat keruh (habis hujan/algae bloom): **150 - 400 NTU**

Kalau hasil tidak masuk akal (mis. >1000 NTU di air tambak yang kelihatan normal), kemungkinan:
- Sensor module bermasalah (sudah pernah kemasukan air)
- Gelembung udara di celah tanduk → goyangkan sensor

## Logging Hasil

Catat tanggal kalibrasi dan hasilnya:

```
Tanggal: ____________
V0   = _______ V (Wadah A: air jernih filter 2x)
V70  = _______ V (Wadah B: 600 ml air + ~1.5 ml susu)
V400 = _______ V (Wadah C: 600 ml air + 1 tutup susu / 5 ml)
Hasil di air tambak = _______ NTU
Catatan: ___________________________
```

## Tips Penting

1. **Setiap pindah wadah**, bilas sensor + lap pelan (jangan keras menggosok jendela optical)
2. **Susu mengendap** dalam 1-2 menit → kocok ulang Wadah B sebelum tiap pengukuran ulang
3. **Sensor vertikal** saat dicelup, tanduk ke bawah, minimal 3 cm terendam
4. **PCB hitam jauh dari air** — sudah pernah kemasukan air, jangan diulang
5. **Suhu air ketiga wadah sama** (suhu ruang) untuk konsistensi

## Untuk Paper / Jurnal

Catat ini sebagai metodologi:
> *"Kalibrasi sensor turbiditas SEN0189 dilakukan dengan metode regresi linear 3-titik piecewise. Referensi 0 NTU menggunakan air galon yang disaring dua kali dengan tisu (estimasi <10 NTU); referensi 70 NTU menggunakan suspensi susu UHT encer (1.5 ml Indomilk dalam 600 ml air mineral); referensi 400 NTU menggunakan suspensi susu UHT pekat (5 ml Indomilk dalam 600 ml air mineral). Dua garis regresi linear dibentuk: garis pertama untuk rentang 0–70 NTU dan garis kedua untuk rentang 70–400 NTU. Pemilihan garis pada saat pembacaan ditentukan oleh tegangan sensor relatif terhadap tegangan acuan V₇₀. Validasi dilakukan dengan pengukuran air tambak udang secara langsung."*

## File Terkait

- Sketch kalibrasi: `kalibrasi/kalibrasi.ino`
- Dokumentasi detail: `KALIBRASI.md`
- Firmware utama: `final_project.ino`
