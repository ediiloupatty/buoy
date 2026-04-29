# Smart Buoy Berbasis IoT untuk Monitoring Kualitas Air Tambak Udang

Sebuah sistem pelampung otonom (*smart buoy*) yang dirancang khusus untuk mengapung di tengah tambak udang. Sistem ini dapat memantau kualitas kelayakan air berdasarkan parameter **Suhu**, **pH**, dan **Kekeruhan** air secara otomatis, *real-time*, dan minim intervensi manusia.

![Diagram Sistem](img/Diagram.png)

## Alur Kerja Sistem (Cara Kerja)

Berbeda dengan sistem pemantauan manual yang memakan waktu atau harus menggunakan prob sensor celup sementara, alat ini dirancang dengan alur kerja mandiri sebagai berikut:

1. **Penempatan Otonom (Smart Buoy)**
   Perangkat ini di desain khusus untuk dapat mengapung secara stabil dan diletakkan langsung di tengah area perairan tambak. *Housing* (cangkang pelindung) bagian dalamnya didesain kedap air demi melindungi seluruh sistem elektronik dari percikan, gelombang air, dan risiko korosi.

2. **Deteksi Kelayakan Lingkungan Air**
   Bagian dasar pelampung memiliki sensor-*sensor waterproof* yang bersentuhan langsung dengan air tambak. Sensor-sensor ini aktif secara konstan membaca tingkat keasaman (pH), suhu air, dan mendeteksi seberapa tinggi partikel kekeruhan di dalam tambak.

3. **Pengolahan & Pemrosesan Data**
   Seluruh data deteksi dari lingkungan air tadi disalurkan ke dalam mikrokontroler utama yang bertugas sebagai otak dari perangkat. Di sini, data analog diproses agar menjadi angka yang mudah dipahami.

4. **Transmisi Jarak Jauh (Nirkabel)**
   Setelah memiliki hasil bacaan, pelampung ini tidak menyimpannya sendiri, melainkan langsung mengirimkan status kualitas air tersebut ke internet atau *cloud server* menggunakan modul *Wi-Fi* internal.

5. **Sistem Daya Berkelanjutan**
   Meski berada di tengah air dan tidak ditancapkan ke listrik PLN, perangkat ini dapat terus hidup 24 jam. Ini karena *smart buoy* memanfaatkan panel surya kecil di bagian atapnya untuk mengisi daya baterai lithium di siang hari.

6. **Pemantauan Cepat oleh Pengguna Tambak**
   Lewat platform *dashboard* atau layanan pemantauan di layar ponsel/laptop, pemilik tambak dapat sewaktu-waktu mengecek apakah parameter air udangnya sedang optimal atau berada di tahap kritis, sehingga pengguna bisa mengambil penanganan (seperti penggantian air atau penambahan oksigen) jauh lebih responsif.

## Panduan Upload ke ESP32 (Flashing / Compilation)

Bagi pengembang atau pengguna yang ingin melakukan pengisian ulang program (*Flashing*) ke *board* ESP32, ikuti panduan standar (*Best Practice*) berikut:

1. **Persiapan Aplikasi:**
   - Gunakan **Arduino IDE** (Versi 2.x disarankan).
   - Pastikan *Board Manager* untuk ESP32 sudah terinstal (Pilih *board* **DOIT ESP32 DEVKIT V1** atau sesuai tipe mikrokontroler yang Anda gunakan).

2. **Daftar Library Wajib (Install via Library Manager):**
   - `Firebase ESP32 Client` (Oleh Mobizt) - Untuk koneksi database nirkabel.
   - `DallasTemperature` (Oleh Miles Burton) & `OneWire` (Oleh Paul Stoffregen) - Untuk membaca sensor suhu.

3. **Inisialisasi Konfigurasi:**
   - Buka file **`Config.h`** di sisi kiri/tab Arduino IDE Anda. 
   - Ubah atribut *Network Credentials* (`ssid` dan `password` Wi-Fi anda).
   - Ubah atribut *Firebase* (`FIREBASE_HOST` & `FIREBASE_AUTH`) menyesuaikan kerahasiaan basis data Anda.

4. **Kompilasi dan Mengunggah (Upload):**
   - Colokkan kabel Micro-USB/Type-C dari soket ESP32 menuju *port* laptop.
   - Pilih *Port* (misalnya `COM3` pada Windows atau `/dev/ttyUSB0` pada alat bersistem Linux) pada menu drop-down atas di aplikasi Arduino IDE.
   - Tekan tanda panah kanan (**Upload**) di pojok kiri atas jendela Arduino IDE.
   - Tunggu hingga proses *Compiling Sketch* selesai. (Tahap ini akan otomatis menjahit `main.ino`, `Sensors.cpp`, dan `Config.h` menjadi satu keping nyawa file *Binary*).
   - Jika terminal putih Arduino bagian bawah menunjukkan pesan `Connecting...`, silakan **tahan tombol tekan fisik BOOT** pada komponen ESP32 selama 2-3 detik lalu lepas, sampai keterangan proses persentase pengunggahan (`Writing...`) menembus angka 100%.

5. **Pengecekan Akhir Sistem:** 
   Bukalah alat *Serial Monitor* (Ikon kaca pembesar / ubah *baud rate* menjadi `115200`) untuk melihat riwayat komunikasi tulisan alat ESP. Perhatikan hingga *board* mencetak pesan `"Connected!"` sebagai pertanda sinyal transmisi berhasil mengudara.

## Integrasi Flutter (Aplikasi Mobile)

Untuk menghubungkan aplikasi Flutter ke Firebase Realtime Database project ini, ikuti langkah berikut:

### Prasyarat

- Flutter SDK terinstal
- Firebase CLI (`npm install -g firebase-tools`)
- FlutterFire CLI (`dart pub global activate flutterfire_cli`)

### Langkah Konfigurasi

1. **Login Firebase** dari terminal:
   ```bash
   firebase login
   ```

2. **Generate konfigurasi otomatis** di root folder project Flutter:
   ```bash
   flutterfire configure --project=monitoring-air-tambak-udang
   ```
   Perintah ini akan otomatis membuat file `lib/firebase_options.dart` dan `android/app/google-services.json`.

3. **Install package** yang diperlukan:
   ```bash
   flutter pub add firebase_core firebase_database
   ```

4. **Inisialisasi Firebase** di `lib/main.dart`:
   ```dart
   import 'package:firebase_core/firebase_core.dart';
   import 'firebase_options.dart';

   void main() async {
     WidgetsFlutterBinding.ensureInitialized();
     await Firebase.initializeApp(
       options: DefaultFirebaseOptions.currentPlatform,
     );
     runApp(const MyApp());
   }
   ```

### Struktur Data

Data sensor dikirim oleh ESP32 ke **2 database berbeda** sesuai fungsinya:

```
ESP32 Smart Buoy
    │
    ├──→ Firebase Realtime DB   → Flutter App (dashboard real-time)
    │    Path: /smart_buoy/live/
    │    Interval: tiap 10 detik
    │
    └──→ Supabase PostgreSQL    → ML Training (tabel, CSV, pandas)
         Tabel: sensor_readings
         Interval: tiap 10 menit
```

#### Firebase `/smart_buoy/live/` — Dashboard Real-Time (tiap 10 detik)

Data ini **ditimpa** setiap 10 detik. Digunakan untuk tampilan dashboard real-time di Flutter.

| Path | Tipe | Contoh | Keterangan |
|---|---|---|---|
| `/smart_buoy/live/suhu` | `double` | `28.5` | Suhu air (°C) |
| `/smart_buoy/live/pH` | `double` | `7.24` | Tingkat keasaman air |
| `/smart_buoy/live/kekeruhan` | `int` | `1200` | Nilai ADC turbidity sensor |

> **Catatan:** Field `status_kekeruhan` tidak dikirim — hitung di sisi Flutter: `<1500` = Jernih, `<3000` = Keruh, `≥3000` = Kotor.

#### Supabase `sensor_readings` — Log Historis untuk ML (tiap 10 menit)

Data disimpan dalam tabel PostgreSQL yang bisa langsung dipakai untuk Machine Learning training.

**Buat tabel di Supabase SQL Editor:**
```sql
CREATE TABLE sensor_readings (
    id          BIGSERIAL PRIMARY KEY,
    tanggal     DATE             NOT NULL,
    jam         TIME             NOT NULL,
    suhu        DOUBLE PRECISION,
    ph          DOUBLE PRECISION,
    kekeruhan   INTEGER,
    status      VARCHAR(10),
    created_at  TIMESTAMPTZ      DEFAULT NOW()
);

-- Index untuk query per tanggal (performa ML export)
CREATE INDEX idx_sensor_tanggal ON sensor_readings(tanggal);
```

**Contoh isi tabel di Supabase Dashboard:**

| id | tanggal | jam | suhu | ph | kekeruhan | status | created_at |
|---|---|---|---|---|---|---|---|
| 1 | 2026-04-29 | 21:00:00 | 28.50 | 7.24 | 1200 | Jernih | 2026-04-29T21:00:00Z |
| 2 | 2026-04-29 | 21:10:00 | 28.30 | 7.18 | 1350 | Jernih | 2026-04-29T21:10:00Z |
| 3 | 2026-04-29 | 21:20:00 | 28.10 | 7.20 | 2800 | Keruh | 2026-04-29T21:20:00Z |

### Contoh Pembacaan Data di Flutter (Real-Time)

```dart
import 'package:firebase_database/firebase_database.dart';

// ── Helper: Hitung status kekeruhan di sisi client ──
String getStatusKekeruhan(int kekeruhan) {
  if (kekeruhan < 1500) return 'Jernih';
  if (kekeruhan < 3000) return 'Keruh';
  return 'Kotor';
}

// ── Dashboard real-time (update tiap 10 detik) ──
final liveRef = FirebaseDatabase.instance.ref('smart_buoy/live');
liveRef.onValue.listen((event) {
  final data = event.snapshot.value as Map<dynamic, dynamic>;
  double suhu = (data['suhu'] ?? 0).toDouble();
  double ph = (data['pH'] ?? 0).toDouble();
  int kekeruhan = (data['kekeruhan'] ?? 0).toInt();
  String status = getStatusKekeruhan(kekeruhan);
});
```

### Contoh Penggunaan Data untuk ML Training (Python)

```python
import pandas as pd
from supabase import create_client

# Koneksi ke Supabase
supabase = create_client(SUPABASE_URL, SUPABASE_KEY)

# Ambil semua data
response = supabase.table('sensor_readings').select('*').execute()
df = pd.DataFrame(response.data)

# Training ML model (contoh: prediksi status air)
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split

X = df[['suhu', 'ph', 'kekeruhan']]
y = df['status']

X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2)
model = RandomForestClassifier()
model.fit(X_train, y_train)
print(f"Akurasi: {model.score(X_test, y_test):.2%}")
```

> **⚠️ Catatan Keamanan:** Jangan commit file `google-services.json`, `GoogleService-Info.plist`, dan kredensial Supabase ke *public repository*. Tambahkan ke `.gitignore`.

