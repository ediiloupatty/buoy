#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
#  PEMBUAT CHART — Hasil Prediksi Double Exponential Smoothing
#  Menghasilkan gambar siap-lampir untuk jurnal/skripsi (300 DPI):
#    1. chart_suhu.png     : Aktual vs Prediksi DES (Suhu)
#    2. chart_ph.png       : Aktual vs Prediksi DES (pH)
#    3. chart_akurasi.png  : Akurasi prediksi per horizon (bar)
#
#  Inisialisasi & rumus SAMA dengan des_manual.py / aplikasi.
#  Jalankan: python3 chart_des.py [data.csv]
# ============================================================

import csv
import sys
from datetime import datetime

import matplotlib
matplotlib.use("Agg")  # tanpa GUI, langsung simpan file
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# ─── KONFIGURASI ───────────────────────────────────────────
ALPHA = 0.5
BETA = 0.5
FORECAST_PERIODS = [3, 6, 9]
# Horizon validasi selaras dengan sistem: 3=30 mnt, 6=60 mnt, 9=90 mnt.
# (Horizon 10 menit tidak dipakai karena sistem hanya memprediksi 30/60/90.)
VALIDASI_HORIZONS = [3, 6, 9]
CSV_FILE = "data_history_24jam.csv"

PARAMS = {
    "Suhu Air (°C)": {"kolom": "suhu", "warna": "#E8553A", "file": "chart_suhu.png",
                       "aman": (26.0, 32.0), "unit": "°C", "dec": 1},
    "pH Air":        {"kolom": "ph",   "warna": "#2E86C1", "file": "chart_ph.png",
                       "aman": (7.5, 8.5),  "unit": "",   "dec": 2},
}


# ─── BACA CSV ──────────────────────────────────────────────
def baca_csv(path):
    with open(path, newline="", encoding="utf-8-sig") as f:
        baris = list(csv.DictReader(f))
    waktu = [datetime.strptime(r["waktu"].strip(), "%Y-%m-%d %H:%M") for r in baris]
    data = {}
    for nama, cfg in PARAMS.items():
        data[nama] = [float((r[cfg["kolom"]] or "0").replace(",", ".")) for r in baris]
    return waktu, data


# ─── DES (Holt) — kembalikan fitted (prediksi 1 langkah) + forecast ──
# Iterasi PERSIS sama dengan hitung_des() di des_manual.py (mulai t=0) agar
# angka akurasi pada chart identik dengan tabel perhitungan manual.
def des(data, alpha, beta, periods):
    level = data[0]
    trend = data[1] - data[0]
    fitted = []
    for t in range(len(data)):
        prev_level, prev_trend = level, trend
        fitted.append(prev_level + prev_trend)          # ramalan 1-langkah utk y_t
        level = alpha * data[t] + (1 - alpha) * (prev_level + prev_trend)
        trend = beta * (level - prev_level) + (1 - beta) * prev_trend
    fitted[0] = None  # titik pertama: prediksi hasil inisialisasi, diabaikan
    forecast = [level + m * trend for m in periods]
    return fitted, forecast, level, trend


# ─── Walk-forward: akurasi tiap horizon (untuk chart bar) ──
def akurasi_per_horizon(data, horizons, alpha, beta):
    hasil = {}
    n = len(data)
    for h in horizons:
        spe = 0.0
        m = 0
        for t in range(1, n - h):
            _, _, lvl, tr = des(data[: t + 1], alpha, beta, [])
            pred = lvl + h * tr
            aktual = data[t + h]
            if aktual != 0:
                spe += abs((aktual - pred) / aktual)
                m += 1
        hasil[h] = (1 - spe / m) * 100 if m else 0.0
    return hasil


def label_horizon(h):
    menit = h * 10
    if menit < 60:
        return f"+{menit}m"
    j, sisa = divmod(menit, 60)
    return f"+{j}j" if sisa == 0 else f"+{j}j{sisa}m"


# ─── CHART 1 & 2: Aktual vs Prediksi DES per parameter ─────
def chart_parameter(nama, waktu, data, cfg):
    fitted, forecast, level, trend = des(data, ALPHA, BETA, FORECAST_PERIODS)

    # Sumbu waktu untuk forecast (lanjut tiap 10 menit)
    interval = (waktu[1] - waktu[0]) if len(waktu) > 1 else None
    waktu_fc = [waktu[-1] + interval * m for m in FORECAST_PERIODS]

    fig, ax = plt.subplots(figsize=(10, 4.5))

    # Rentang aman (arsir hijau lembut)
    amin, amax = cfg["aman"]
    ax.axhspan(amin, amax, color="#27AE60", alpha=0.08, zorder=0,
               label=f"Rentang aman ({amin}–{amax}{cfg['unit']})")

    # Garis data aktual
    ax.plot(waktu, data, color=cfg["warna"], lw=1.8, label="Data aktual", zorder=3)

    # Garis prediksi 1-langkah (fitted) — putus-putus
    wk_fit = [w for w, f in zip(waktu, fitted) if f is not None]
    val_fit = [f for f in fitted if f is not None]
    ax.plot(wk_fit, val_fit, color="#7D3C98", lw=1.4, ls="--",
            label="Prediksi DES (1 langkah)", zorder=2)

    # Forecast ke depan (titik + garis)
    ax.plot([waktu[-1]] + waktu_fc, [data[-1]] + forecast, color="#C0392B",
            lw=1.6, ls=":", marker="o", ms=5, label="Prediksi ke depan (+30/60/90 mnt)",
            zorder=4)
    # Label nilai prediksi — selang-seling atas/bawah agar tidak menumpuk
    for i, (w, v) in enumerate(zip(waktu_fc, forecast)):
        dy = 9 if i % 2 == 0 else -14
        ax.annotate(f"{v:.{cfg['dec']}f}", (w, v), textcoords="offset points",
                    xytext=(0, dy), ha="center", fontsize=7.5,
                    fontweight="bold", color="#C0392B")

    ax.set_title(f"Prediksi {nama} dengan Double Exponential Smoothing (α={ALPHA}, β={BETA})",
                 fontsize=11, fontweight="bold")
    ax.set_xlabel("Waktu", fontsize=9)
    ax.set_ylabel(nama, fontsize=9)
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    ax.xaxis.set_major_locator(mdates.AutoDateLocator())
    ax.grid(True, ls="--", lw=0.5, alpha=0.4)
    ax.legend(fontsize=8, loc="best", framealpha=0.9)
    fig.autofmt_xdate(rotation=0, ha="center")
    fig.tight_layout()
    fig.savefig(cfg["file"], dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"  ✔ {cfg['file']}")


# ─── CHART 3: Bar akurasi per horizon (kedua parameter) ────
def chart_akurasi(data_dict):
    horizons = VALIDASI_HORIZONS
    labels = [label_horizon(h) for h in horizons]
    nama_list = list(data_dict.keys())

    akur = {nama: akurasi_per_horizon(data_dict[nama], horizons, ALPHA, BETA)
            for nama in nama_list}

    x = range(len(horizons))
    width = 0.38
    fig, ax = plt.subplots(figsize=(8, 4.5))

    for i, nama in enumerate(nama_list):
        nilai = [akur[nama][h] for h in horizons]
        pos = [xi + (i - 0.5) * width for xi in x]
        bars = ax.bar(pos, nilai, width, label=nama,
                      color=PARAMS[nama]["warna"], alpha=0.88)
        for b, v in zip(bars, nilai):
            ax.annotate(f"{v:.2f}%", (b.get_x() + b.get_width() / 2, v),
                        textcoords="offset points", xytext=(0, 3),
                        ha="center", fontsize=7.5, fontweight="bold")

    ax.set_title("Akurasi Prediksi DES per Horizon (Walk-Forward Validation)",
                 fontsize=11, fontweight="bold")
    ax.set_xlabel("Horizon prediksi", fontsize=9)
    ax.set_ylabel("Akurasi (%)", fontsize=9)
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels)
    ax.set_ylim(95, 100.3)
    ax.grid(True, axis="y", ls="--", lw=0.5, alpha=0.4)
    ax.legend(fontsize=8.5)
    fig.tight_layout()
    fig.savefig("chart_akurasi.png", dpi=300, bbox_inches="tight")
    plt.close(fig)
    print("  ✔ chart_akurasi.png")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else CSV_FILE
    waktu, data = baca_csv(path)
    print(f"Sumber data: {path}  ({len(waktu)} titik)")
    print("Menyimpan chart:")
    for nama, cfg in PARAMS.items():
        chart_parameter(nama, waktu, data[nama], cfg)
    chart_akurasi(data)
    print("Selesai.")


if __name__ == "__main__":
    main()
