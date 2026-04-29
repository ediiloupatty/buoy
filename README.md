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

### Struktur Data di Firebase Realtime Database

Data sensor dikirim oleh ESP32 ke Firebase dengan **2 path** berbeda:

#### Path `/smart_buoy/live/` — Data Real-Time (update tiap 10 detik)

Data ini **ditimpa** setiap 10 detik. Digunakan untuk tampilan dashboard real-time.

| Path | Tipe | Contoh | Keterangan |
|---|---|---|---|
| `/smart_buoy/live/suhu` | `double` | `28.5` | Suhu air (°C) |
| `/smart_buoy/live/pH` | `double` | `7.24` | Tingkat keasaman air |
| `/smart_buoy/live/kekeruhan` | `int` | `1200` | Nilai ADC turbidity sensor |

> **Catatan:** Field `status_kekeruhan` tidak dikirim di live data untuk menghemat bandwidth. Hitung di sisi Flutter: `<1500` = Jernih, `<3000` = Keruh, `≥3000` = Kotor.

#### Path `/smart_buoy/history/{timestamp}` — Log Historis (tersimpan tiap 10 menit)

Data ini **tidak ditimpa** — setiap 10 menit ditambahkan record baru. Digunakan untuk grafik riwayat dan analisis.

| Field | Tipe | Keterangan |
|---|---|---|
| `suhu` | `double` | Suhu air saat itu |
| `pH` | `double` | Tingkat pH saat itu |
| `kekeruhan` | `int` | Nilai kekeruhan saat itu |
| `status_kekeruhan` | `String` | Status: Jernih / Keruh / Kotor |
| `timestamp` | `long` | Server timestamp Firebase (otomatis) |

> **Catatan:** Path `/history/` otomatis terbuat di Firebase setelah ESP32 berjalan 10 menit pertama.

### Contoh Pembacaan Data di Flutter

```dart
import 'package:firebase_database/firebase_database.dart';

// ── Dashboard real-time (update tiap 10 detik) ──
final liveRef = FirebaseDatabase.instance.ref('smart_buoy/live');
liveRef.onValue.listen((event) {
  final data = event.snapshot.value as Map<dynamic, dynamic>;
  double suhu = (data['suhu'] ?? 0).toDouble();
  double ph = (data['pH'] ?? 0).toDouble();
  int kekeruhan = (data['kekeruhan'] ?? 0).toInt();
  String status = data['status_kekeruhan'] ?? 'N/A';
});

// ── Riwayat data (tersimpan tiap 10 menit) ──
final historyRef = FirebaseDatabase.instance.ref('smart_buoy/history');
historyRef.orderByChild('timestamp').limitToLast(50).onValue.listen((event) {
  // Ambil 50 data terakhir untuk grafik riwayat
});
```

> **⚠️ Catatan Keamanan:** Jangan commit file `google-services.json` (Android) dan `GoogleService-Info.plist` (iOS) ke *public repository*. Tambahkan ke `.gitignore`.
