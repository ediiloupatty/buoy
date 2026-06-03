#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
#  TUNING PARAMETER DES — Uji kombinasi alpha & beta
#  Mencari nilai alpha (pemulusan level) & beta (pemulusan tren)
#  yang menghasilkan error prediksi terkecil (akurasi tertinggi).
#
#  Metode validasi: walk-forward / rolling origin (sama dgn des_manual.py)
#  pada horizon 30/60/90 menit. Metrik diringkas = rata-rata antar horizon.
#
#  Output:
#    • Tabel grid akurasi (alpha × beta) per parameter di terminal
#    • 5 kombinasi terbaik per parameter
#    • Heatmap  : tuning_heatmap.png  (Suhu & pH bersebelahan)
#
#  Jalankan: python3 tuning_des.py [data.csv]
# ============================================================

import csv
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ─── KONFIGURASI ───────────────────────────────────────────
ALPHAS = [round(0.1 * i, 1) for i in range(1, 10)]   # 0.1 .. 0.9
BETAS = [round(0.1 * i, 1) for i in range(1, 10)]    # 0.1 .. 0.9
HORIZONS = [3, 6, 9]                                  # 30/60/90 menit
CSV_FILE = "data_history_24jam.csv"

PARAMS = {
    "Suhu Air (°C)": {"kolom": "suhu", "file_cmap": "Oranges"},
    "pH Air":        {"kolom": "ph",   "file_cmap": "Blues"},
}


# ─── BACA CSV ──────────────────────────────────────────────
def baca_csv(path):
    with open(path, newline="", encoding="utf-8-sig") as f:
        baris = list(csv.DictReader(f))
    data = {}
    for nama, cfg in PARAMS.items():
        data[nama] = [float((r[cfg["kolom"]] or "0").replace(",", ".")) for r in baris]
    return data


# ─── DES (Holt) — kembalikan level & trend akhir ───────────
def des_level_trend(data, alpha, beta):
    level = data[0]
    trend = data[1] - data[0]
    for t in range(1, len(data)):
        prev_level = level
        level = alpha * data[t] + (1 - alpha) * (prev_level + trend)
        trend = beta * (level - prev_level) + (1 - beta) * trend
    return level, trend


# ─── Walk-forward: metrik gabungan utk satu (alpha, beta) ──
def evaluasi(data, alpha, beta, horizons):
    """Kembalikan dict {mae, rmse, mape, akurasi} rata-rata antar horizon."""
    n = len(data)
    sae = sse = spe = 0.0
    m = 0
    for h in horizons:
        for t in range(1, n - h):
            level, trend = des_level_trend(data[: t + 1], alpha, beta)
            pred = level + h * trend
            aktual = data[t + h]
            err = aktual - pred
            sae += abs(err)
            sse += err * err
            if aktual != 0:
                spe += abs(err / aktual)
            m += 1
    if m == 0:
        return None
    mape = spe / m * 100
    return {
        "mae": sae / m,
        "rmse": (sse / m) ** 0.5,
        "mape": mape,
        "akurasi": 100 - mape,
    }


# ─── Cetak tabel grid + top-5, kembalikan matriks akurasi ──
def proses_parameter(nama, data):
    # matriks[alpha_idx][beta_idx] = metrik
    hasil = {}
    for a in ALPHAS:
        for b in BETAS:
            hasil[(a, b)] = evaluasi(data, a, b, HORIZONS)

    print("=" * 78)
    print(f"  PARAMETER: {nama}   (akurasi % rata-rata horizon 30/60/90 mnt)")
    print("=" * 78)

    # ── Grid akurasi: baris = alpha, kolom = beta ──
    header = "  α \\ β |" + "".join(f"{b:>7}" for b in BETAS)
    print(header)
    print("  " + "-" * (len(header) - 2))
    best = max(hasil, key=lambda k: hasil[k]["akurasi"])
    for a in ALPHAS:
        row = f"  {a:>5} |"
        for b in BETAS:
            val = hasil[(a, b)]["akurasi"]
            mark = "*" if (a, b) == best else " "
            row += f"{val:>6.2f}{mark}"
        print(row)
    print("  " + "-" * (len(header) - 2))
    print("  (* = kombinasi terbaik)")
    print()

    # ── Top-5 kombinasi terbaik (metrik lengkap) ──
    urut = sorted(hasil.items(), key=lambda kv: kv[1]["mape"])
    print("  5 KOMBINASI TERBAIK (error terkecil):")
    print(f"  {'Rank':>4} | {'α':>4} | {'β':>4} | {'MAE':>8} | {'RMSE':>8} | "
          f"{'MAPE':>7} | {'Akurasi':>8}")
    print("  " + "-" * 64)
    for i, ((a, b), met) in enumerate(urut[:5], 1):
        print(f"  {i:>4} | {a:>4} | {b:>4} | {met['mae']:>8.4f} | "
              f"{met['rmse']:>8.4f} | {met['mape']:>6.2f}% | {met['akurasi']:>7.2f}%")
    print("  " + "-" * 64)
    ba, bb = best
    bm = hasil[best]
    print(f"  >> TERBAIK: α={ba}, β={bb}  →  akurasi {bm['akurasi']:.2f}% "
          f"(MAE {bm['mae']:.4f}, RMSE {bm['rmse']:.4f}, MAPE {bm['mape']:.2f}%)")
    print()

    # matriks akurasi utk heatmap
    matriks = [[hasil[(a, b)]["akurasi"] for b in BETAS] for a in ALPHAS]
    return matriks, best, hasil


# ─── Heatmap ───────────────────────────────────────────────
def buat_heatmap(matriks_per_param, best_per_param):
    fig, axes = plt.subplots(1, len(PARAMS), figsize=(13, 5.5))
    if len(PARAMS) == 1:
        axes = [axes]

    for ax, (nama, cfg) in zip(axes, PARAMS.items()):
        matriks = matriks_per_param[nama]
        best = best_per_param[nama]
        im = ax.imshow(matriks, cmap=cfg["file_cmap"], aspect="auto", origin="lower")

        ax.set_xticks(range(len(BETAS)))
        ax.set_xticklabels([f"{b}" for b in BETAS])
        ax.set_yticks(range(len(ALPHAS)))
        ax.set_yticklabels([f"{a}" for a in ALPHAS])
        ax.set_xlabel("β (pemulusan tren)", fontsize=9)
        ax.set_ylabel("α (pemulusan level)", fontsize=9)
        ax.set_title(f"Akurasi Prediksi DES — {nama}", fontsize=10, fontweight="bold")

        # Anotasi tiap sel
        for i, a in enumerate(ALPHAS):
            for j, b in enumerate(BETAS):
                val = matriks[i][j]
                is_best = (a, b) == best
                ax.text(j, i, f"{val:.1f}", ha="center", va="center",
                        fontsize=6.5,
                        color="white" if is_best else "black",
                        fontweight="bold" if is_best else "normal")
                if is_best:
                    ax.add_patch(plt.Rectangle((j - 0.5, i - 0.5), 1, 1,
                                 fill=False, edgecolor="red", lw=2.2))

        cb = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        cb.set_label("Akurasi (%)", fontsize=8)

    fig.suptitle("Tuning Parameter DES (α × β) — Walk-Forward, Horizon 30/60/90 menit",
                 fontsize=12, fontweight="bold")
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig("tuning_heatmap.png", dpi=300, bbox_inches="tight")
    plt.close(fig)
    print("Heatmap disimpan: tuning_heatmap.png")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else CSV_FILE
    data = baca_csv(path)
    print()
    print("#" * 78)
    print("#  TUNING PARAMETER DES — pencarian alpha & beta optimal")
    print(f"#  Sumber data : {path}  ({len(next(iter(data.values())))} titik)")
    print(f"#  Grid        : α,β = {ALPHAS[0]}..{ALPHAS[-1]} (step 0.1) → "
          f"{len(ALPHAS) * len(BETAS)} kombinasi")
    print(f"#  Horizon     : 30/60/90 menit (walk-forward)")
    print("#" * 78)
    print()

    matriks_per_param, best_per_param = {}, {}
    for nama, nilai in data.items():
        matriks, best, _ = proses_parameter(nama, nilai)
        matriks_per_param[nama] = matriks
        best_per_param[nama] = best

    buat_heatmap(matriks_per_param, best_per_param)
    print("Selesai.")


if __name__ == "__main__":
    main()
