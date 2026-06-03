#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
#  PERHITUNGAN MANUAL — Double Exponential Smoothing (DES)
#  Metode: Holt's Linear Trend Method
#
#  Tujuan : memvalidasi nilai prediksi yang muncul di aplikasi
#           mobile (lib/utils/des_algorithm.dart) secara manual,
#           lengkap dengan tabel iterasi & error untuk lampiran skripsi.
#
#  Rumus:
#    Level    : L_t = a * y_t + (1 - a) * (L_{t-1} + T_{t-1})
#    Trend    : T_t = b * (L_t - L_{t-1}) + (1 - b) * T_{t-1}
#    Forecast : y_hat_{t+m} = L_t + m * T_t
#
#  Inisialisasi (SAMA dengan aplikasi):
#    L_1 = y_1
#    T_1 = y_2 - y_1
#
#  Sumber data: file CSV (lihat KONFIGURASI di bawah).
#  Jalankan   : python3 des_manual.py
#               python3 des_manual.py data_lain.csv   (pakai CSV lain)
# ============================================================

import csv
import sys
from datetime import datetime

# ─── KONFIGURASI PARAMETER (samakan dengan DESConfig di app) ───
ALPHA = 0.5            # faktor pemulusan level  (0 < a < 1)
BETA = 0.5             # faktor pemulusan tren   (0 < b < 1)
FORECAST_PERIODS = [3, 6, 9]   # m langkah ke depan

# Horizon yang divalidasi (akurasi prediksi sebenarnya, walk-forward).
# Selaras dengan FORECAST_PERIODS: 3 = 30 mnt, 6 = 60 mnt, 9 = 90 mnt.
# (Horizon 1 / 10 menit TIDAK dipakai karena sistem hanya memprediksi 30/60/90.)
VALIDASI_HORIZONS = [3, 6, 9]

# ─── KONFIGURASI CSV ───────────────────────────────────────
# File CSV default (bisa ditimpa lewat argumen command line).
CSV_FILE = "data_history.csv"

# Nama kolom di file CSV.
KOLOM_WAKTU = "waktu"   # opsional; kosongkan ("") bila tak ada kolom waktu
FORMAT_WAKTU = "%Y-%m-%d %H:%M"   # format teks pada kolom waktu

# Pemetaan: "Label tampilan" -> "nama kolom di CSV"
KOLOM_PARAMETER = {
    "Suhu Air (°C)": "suhu",
    "pH Air": "ph",
}

# Ambil hanya N baris TERAKHIR (mis. 10). Set 0 untuk memakai semua baris.
# Untuk validasi akurasi prediksi, makin banyak data makin baik → default 0 (semua).
AMBIL_N_TERAKHIR = 0

# Interval sensor (menit) bila kolom waktu tidak ada / tak bisa dibaca.
INTERVAL_FALLBACK_MENIT = 10

# Rentang aman (untuk penilaian status Aman/Bahaya pada hasil prediksi)
RENTANG_AMAN = {
    "Suhu Air (°C)": (26.0, 32.0),
    "pH Air": (7.5, 8.5),
}

# Jumlah angka di belakang koma saat menampilkan tiap parameter
DESIMAL = {
    "Suhu Air (°C)": 2,
    "pH Air": 2,
}


# ============================================================
#  BACA DATA DARI CSV
# ============================================================
def baca_csv(path):
    """Kembalikan (datasets, waktu_list).

    datasets : dict {label -> list[float]}
    waktu_list : list[datetime] (boleh kosong bila tak ada kolom waktu)
    """
    with open(path, newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        baris = list(reader)

    if not baris:
        raise ValueError(f"File '{path}' kosong / tidak ada baris data.")

    # Validasi kolom yang dibutuhkan ada di header.
    header = reader.fieldnames or []
    for label, kolom in KOLOM_PARAMETER.items():
        if kolom not in header:
            raise ValueError(
                f"Kolom '{kolom}' (untuk {label}) tidak ada di CSV. "
                f"Header terbaca: {header}")

    # Ambil N baris terakhir bila diminta.
    if AMBIL_N_TERAKHIR and len(baris) > AMBIL_N_TERAKHIR:
        baris = baris[-AMBIL_N_TERAKHIR:]

    datasets = {}
    for label, kolom in KOLOM_PARAMETER.items():
        nilai = []
        for i, r in enumerate(baris, 1):
            teks = (r.get(kolom) or "").strip().replace(",", ".")
            try:
                nilai.append(float(teks))
            except ValueError:
                raise ValueError(
                    f"Nilai tidak valid di kolom '{kolom}' baris ke-{i}: "
                    f"'{r.get(kolom)}'")
        datasets[label] = nilai

    # Baca kolom waktu bila tersedia.
    waktu_list = []
    if KOLOM_WAKTU and KOLOM_WAKTU in header:
        for r in baris:
            teks = (r.get(KOLOM_WAKTU) or "").strip()
            try:
                waktu_list.append(datetime.strptime(teks, FORMAT_WAKTU))
            except (ValueError, TypeError):
                waktu_list = []   # satu gagal -> abaikan seluruh kolom waktu
                break

    return datasets, waktu_list


def interval_menit_dari_waktu(waktu_list):
    """Median selisih antar timestamp (menit), seperti di aplikasi.
    Fallback ke INTERVAL_FALLBACK_MENIT bila data tak memadai."""
    if len(waktu_list) < 2:
        return INTERVAL_FALLBACK_MENIT
    gaps = []
    for i in range(1, len(waktu_list)):
        detik = (waktu_list[i] - waktu_list[i - 1]).total_seconds()
        if detik > 0:
            gaps.append(detik)
    if not gaps:
        return INTERVAL_FALLBACK_MENIT
    gaps.sort()
    return gaps[len(gaps) // 2] / 60.0   # median, dalam menit


# ============================================================
#  INTI PERHITUNGAN DES (mirror dari des_algorithm.dart)
# ============================================================
def hitung_des(data, alpha, beta, periods):
    """Kembalikan (tabel_iterasi, forecast, level_akhir, trend_akhir)."""
    if len(data) < 2:
        return [], [], None, None

    # Inisialisasi — sama persis dengan aplikasi
    level = data[0]
    trend = data[1] - data[0]

    tabel = []  # tiap baris: dict per langkah iterasi
    for t in range(len(data)):
        prev_level = level
        prev_trend = trend

        # fitted = prediksi 1 langkah ke depan dari titik sebelumnya
        fitted = prev_level + prev_trend

        # L_t = a*y_t + (1-a)*(L_{t-1} + T_{t-1})
        level = alpha * data[t] + (1 - alpha) * (prev_level + prev_trend)
        # T_t = b*(L_t - L_{t-1}) + (1-b)*T_{t-1}
        trend = beta * (level - prev_level) + (1 - beta) * prev_trend

        error = data[t] - fitted
        tabel.append({
            "t": t + 1,
            "y": data[t],
            "level": level,
            "trend": trend,
            "fitted": fitted,
            "error": error,
        })

    # Forecast: y_hat_{t+m} = L_t + m * T_t
    forecast = [(m, level + m * trend) for m in periods if m > 0]
    return tabel, forecast, level, trend


# ============================================================
#  METRIK ERROR (validasi akurasi)
# ============================================================
def hitung_error(tabel):
    """MAE, RMSE, MAPE. Baris pertama dilewati (warm-up: fitted = y_2)."""
    # Lewati t=1 karena fitted-nya hasil inisialisasi, belum 'belajar'.
    baris = [r for r in tabel if r["t"] >= 2]
    n = len(baris)
    if n == 0:
        return None

    sae = sum(abs(r["error"]) for r in baris)              # sum abs error
    sse = sum(r["error"] ** 2 for r in baris)              # sum squared error
    spe = sum(abs(r["error"] / r["y"]) for r in baris if r["y"] != 0)

    mae = sae / n
    rmse = (sse / n) ** 0.5
    mape = (spe / n) * 100
    return {"n": n, "mae": mae, "rmse": rmse, "mape": mape}


# ============================================================
#  VALIDASI AKURASI PREDIKSI — Walk-Forward / Rolling Origin
# ============================================================
# Inilah AKURASI PREDIKSI yang sebenarnya (untuk jurnal), beda dari error
# fitting di atas. Prosedur di tiap titik asal (origin) t:
#   1. Latih DES HANYA dengan data y_1 .. y_t  (seolah masa depan belum diketahui)
#   2. Ramal h langkah ke depan: y_hat = L_t + h * T_t
#   3. Bandingkan dengan nilai AKTUAL y_{t+h} yang benar-benar terjadi
# Origin digeser maju satu per satu → banyak pasangan (prediksi, aktual).
def _level_trend(data, alpha, beta):
    """Kembalikan (level, trend) DES di akhir [data]. Butuh >= 2 titik."""
    _, _, lvl, tr = hitung_des(data, alpha, beta, [])
    return lvl, tr


def validasi_prediksi(data, horizons, alpha, beta):
    """Walk-forward untuk tiap horizon. Kembalikan dict:
       { h: {"n","mae","rmse","mape","detail":[(idx_origin, aktual, prediksi, err)]} }
    """
    n = len(data)
    hasil = {}
    for h in horizons:
        detail = []
        # origin t (0-based) butuh minimal 2 titik latih → mulai t=1
        # dan harus ada aktual di t+h → t maksimal n-1-h
        for t in range(1, n - h):
            level, trend = _level_trend(data[: t + 1], alpha, beta)
            prediksi = level + h * trend
            aktual = data[t + h]
            detail.append((t, aktual, prediksi, aktual - prediksi))

        m = len(detail)
        if m == 0:
            hasil[h] = None
            continue
        sae = sum(abs(e) for *_, e in detail)
        sse = sum(e * e for *_, e in detail)
        spe = sum(abs(e / a) for _, a, _, e in detail if a != 0)
        hasil[h] = {
            "n": m,
            "mae": sae / m,
            "rmse": (sse / m) ** 0.5,
            "mape": (spe / m) * 100,
            "detail": detail,
        }
    return hasil


# ============================================================
#  TAMPILAN
# ============================================================
def label_waktu(m, interval_menit):
    total = interval_menit * m
    total_int = round(total)
    if total_int >= 60:
        j, sisa = divmod(total_int, 60)
        return f"+{j}j {sisa}m" if sisa else f"+{j}j"
    return f"+{total_int} mnt"


def cetak_parameter(nama, data, interval_menit):
    dec = DESIMAL.get(nama, 2)
    fmt = f"{{:.{dec}f}}"
    aman_min, aman_max = RENTANG_AMAN.get(nama, (float("-inf"), float("inf")))

    print("=" * 72)
    print(f"  PARAMETER: {nama}")
    print("=" * 72)
    print(f"  Jumlah data    : {len(data)}")
    print(f"  Alpha (a)      : {ALPHA}")
    print(f"  Beta  (b)      : {BETA}")
    print(f"  Inisialisasi   : L1 = y1 = {fmt.format(data[0])} ; "
          f"T1 = y2 - y1 = {fmt.format(data[1] - data[0])}")
    print()

    tabel, forecast, level_akhir, trend_akhir = hitung_des(
        data, ALPHA, BETA, FORECAST_PERIODS)

    # ── Tabel iterasi ───────────────────────────────────────
    # Data besar → tampilkan hanya 5 awal + 5 akhir (perhitungan tetap penuh).
    print("  TABEL ITERASI SMOOTHING")
    print("  " + "-" * 68)
    print(f"  {'t':>2} | {'y_t':>8} | {'Level L_t':>10} | {'Trend T_t':>10} | "
          f"{'Fitted':>9} | {'Error':>8}")
    print("  " + "-" * 68)

    def _cetak_baris(r):
        fitted_str = fmt.format(r["fitted"]) if r["t"] >= 2 else "-"
        err_str = fmt.format(r["error"]) if r["t"] >= 2 else "-"
        # t=1 fitted-nya hasil init (y2); tandai '-' agar tak rancu.
        if r["t"] == 1:
            fitted_str, err_str = "(init)", "-"
        print(f"  {r['t']:>2} | {fmt.format(r['y']):>8} | "
              f"{fmt.format(r['level']):>10} | {fmt.format(r['trend']):>10} | "
              f"{fitted_str:>9} | {err_str:>8}")

    BATAS_TAMPIL = 30
    if len(tabel) <= BATAS_TAMPIL:
        for r in tabel:
            _cetak_baris(r)
    else:
        for r in tabel[:5]:
            _cetak_baris(r)
        print(f"  {'...':>2} |   (… {len(tabel) - 10} baris tengah disembunyikan …)")
        for r in tabel[-5:]:
            _cetak_baris(r)
    print("  " + "-" * 68)
    print(f"  Level akhir (L_t) = {fmt.format(level_akhir)}   "
          f"Trend akhir (T_t) = {fmt.format(trend_akhir)}")
    print()

    # ── Error / akurasi ─────────────────────────────────────
    err = hitung_error(tabel)
    if err:
        print("  AKURASI (terhadap data historis, t>=2)")
        print(f"    MAE  = {err['mae']:.4f}")
        print(f"    RMSE = {err['rmse']:.4f}")
        print(f"    MAPE = {err['mape']:.2f} %   "
              f"(akurasi ~ {100 - err['mape']:.2f} %)")
        print()

    # ── Hasil prediksi ke depan ─────────────────────────────
    print("  HASIL PREDIKSI (y_hat = L_t + m * T_t)")
    print("  " + "-" * 56)
    print(f"  {'m':>2} | {'Waktu':>9} | {'Prediksi':>12} | {'Status':>8}")
    print("  " + "-" * 56)
    for m, nilai in forecast:
        status = "Aman" if aman_min <= nilai <= aman_max else "BAHAYA"
        hitung = f"{fmt.format(level_akhir)} + {m}*({fmt.format(trend_akhir)})"
        print(f"  {m:>2} | {label_waktu(m, interval_menit):>9} | "
              f"{fmt.format(nilai):>12} | {status:>8}    [{hitung}]")
    print("  " + "-" * 56)
    print()


def cetak_validasi(nama, data, waktu_list, interval_menit):
    """Cetak validasi akurasi prediksi (walk-forward) + tabel detail h=1."""
    dec = DESIMAL.get(nama, 2)
    fmt = f"{{:.{dec}f}}"

    hasil = validasi_prediksi(data, VALIDASI_HORIZONS, ALPHA, BETA)

    print("  VALIDASI AKURASI PREDIKSI (walk-forward / rolling origin)")
    print("  Tiap titik: latih DES s/d t → ramal h langkah → bandingkan AKTUAL t+h")
    print("  " + "-" * 60)
    print(f"  {'Horizon':>10} | {'n':>3} | {'MAE':>7} | {'RMSE':>7} | "
          f"{'MAPE':>7} | {'Akurasi':>8}")
    print("  " + "-" * 60)
    for h in VALIDASI_HORIZONS:
        r = hasil.get(h)
        label = label_waktu(h, interval_menit)
        if not r:
            print(f"  {label:>10} | {'-':>3} | data tidak cukup untuk horizon ini")
            continue
        print(f"  {label:>10} | {r['n']:>3} | {r['mae']:>7.4f} | "
              f"{r['rmse']:>7.4f} | {r['mape']:>6.2f}% | "
              f"{100 - r['mape']:>6.2f} %")
    print("  " + "-" * 60)
    print()

    # ── Detail walk-forward untuk horizon terkecil (paling banyak titik) ──
    h0 = VALIDASI_HORIZONS[0]
    r0 = hasil.get(h0)
    if r0:
        print(f"  DETAIL PER TITIK (horizon {label_waktu(h0, interval_menit)} "
              f"— prediksi vs aktual)")
        print("  " + "-" * 60)
        print(f"  {'Waktu aktual':>16} | {'Prediksi':>9} | {'Aktual':>8} | "
              f"{'Error':>7} | {'%Err':>6}")
        print("  " + "-" * 60)
        for (t, aktual, prediksi, err) in r0["detail"]:
            idx_aktual = t + h0
            if idx_aktual < len(waktu_list):
                wkt = waktu_list[idx_aktual].strftime("%Y-%m-%d %H:%M")
            else:
                wkt = f"data ke-{idx_aktual + 1}"
            pct = abs(err / aktual) * 100 if aktual != 0 else 0.0
            print(f"  {wkt:>16} | {fmt.format(prediksi):>9} | "
                  f"{fmt.format(aktual):>8} | {fmt.format(err):>7} | {pct:>5.2f}%")
        print("  " + "-" * 60)
        print()


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else CSV_FILE

    try:
        datasets, waktu_list = baca_csv(path)
    except FileNotFoundError:
        print(f"[ERROR] File CSV tidak ditemukan: '{path}'")
        sys.exit(1)
    except ValueError as e:
        print(f"[ERROR] {e}")
        sys.exit(1)

    interval_menit = interval_menit_dari_waktu(waktu_list)
    sumber_interval = "dari kolom waktu" if waktu_list else "fallback (default)"

    print()
    print("#" * 72)
    print("#  PERHITUNGAN MANUAL DES (Holt's Linear Trend)")
    print(f"#  Sumber data    : {path}")
    print(f"#  Interval sensor: {interval_menit:.1f} menit ({sumber_interval})")
    print(f"#  Forecast       : m = {FORECAST_PERIODS}")
    print("#" * 72)
    print()

    for nama, data in datasets.items():
        cetak_parameter(nama, data, interval_menit)
        cetak_validasi(nama, data, waktu_list, interval_menit)


if __name__ == "__main__":
    main()
