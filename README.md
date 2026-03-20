# Smart Buoy Berbasis IoT untuk Monitoring Kualitas Air Tambak Udang

Sebuah sistem pelampung otonom (*smart buoy*) yang dirancang khusus untuk mengapung di tengah tambak udang. Sistem ini dapat memantau kualitas kelayakan air berdasarkan parameter **Suhu**, **pH**, dan **Kekeruhan** air secara otomatis, *real-time*, dan minim intervensi manusia.

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
