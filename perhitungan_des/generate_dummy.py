#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================
#  GENERATOR DATA DUMMY (SIMULASI) — untuk MENGUJI program DES
#
#  ⚠️ PENTING: Ini DATA SIMULASI/SINTETIS, BUKAN hasil pengukuran
#     tambak asli. Pakai HANYA untuk menguji apakah program & algoritma
#     bekerja. JANGAN disajikan sebagai data lapangan di skripsi/jurnal.
#
#  Karakteristik yang ditiru (realistis untuk tambak udang):
#    • Suhu: rata-rata ~29°C, naik siang (puncak ~15:00), turun dini hari
#    • pH  : rata-rata ~7.8, naik sore (puncak ~16:00, fotosintesis), turun malam
#    • Ada noise acak + sedikit variasi antar-hari (biar tidak sempurna)
#
#  Output: data_dummy.csv (kolom: waktu,suhu,ph)
#  Jalankan: python3 generate_dummy.py
# ============================================================

import csv
import math
import random
from datetime import datetime, timedelta

# ─── KONFIGURASI SIMULASI ──────────────────────────────────
SEED            = 42            # biar hasil bisa diulang (reproducible)
HARI            = 7             # berapa hari direkam
INTERVAL_MENIT  = 10           # jarak antar data (menit)
MULAI           = datetime(2026, 5, 1, 0, 0)  # waktu awal (WIB)
OUTPUT          = "data_dummy.csv"

# Suhu (°C)
SUHU_RATA       = 29.0          # rata-rata harian
SUHU_AMPLITUDO  = 1.8           # selisih puncak-lembah dari rata-rata
SUHU_PUNCAK_JAM = 15.0          # jam suhu tertinggi
SUHU_NOISE      = 0.12          # simpangan noise acak

# pH
PH_RATA         = 7.80
PH_AMPLITUDO    = 0.25
PH_PUNCAK_JAM   = 16.0
PH_NOISE        = 0.02

# Variasi antar-hari (offset tetap per hari, biar tak sempurna periodik)
SUHU_VAR_HARIAN = 0.30
PH_VAR_HARIAN   = 0.05


def siklus_harian(jam, amplitudo, jam_puncak):
    """Pola sinus harian: maksimum di jam_puncak, minimum 12 jam setelahnya."""
    return amplitudo * math.cos(2 * math.pi * (jam - jam_puncak) / 24.0)


def main():
    random.seed(SEED)
    n = int(HARI * 24 * 60 / INTERVAL_MENIT)

    # offset acak per hari (konstan dalam 1 hari)
    offset_suhu = {d: random.gauss(0, SUHU_VAR_HARIAN) for d in range(HARI + 1)}
    offset_ph   = {d: random.gauss(0, PH_VAR_HARIAN)   for d in range(HARI + 1)}

    rows = []
    for i in range(n):
        dt = MULAI + timedelta(minutes=i * INTERVAL_MENIT)
        jam = dt.hour + dt.minute / 60.0
        hari_ke = (dt - MULAI).days

        suhu = (SUHU_RATA + offset_suhu[hari_ke]
                + siklus_harian(jam, SUHU_AMPLITUDO, SUHU_PUNCAK_JAM)
                + random.gauss(0, SUHU_NOISE))
        ph = (PH_RATA + offset_ph[hari_ke]
              + siklus_harian(jam, PH_AMPLITUDO, PH_PUNCAK_JAM)
              + random.gauss(0, PH_NOISE))

        # batasi ke rentang wajar + bulatkan seperti sensor asli
        suhu = round(max(26.0, min(32.0, suhu)), 1)
        ph   = round(max(7.30, min(8.30, ph)), 2)
        rows.append((dt.strftime("%Y-%m-%d %H:%M"), suhu, ph))

    with open(OUTPUT, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["waktu", "suhu", "ph"])
        w.writerows(rows)

    print(f"✓ {len(rows)} data simulasi ditulis ke {OUTPUT}")
    print(f"  Periode : {rows[0][0]}  →  {rows[-1][0]}  ({HARI} hari)")
    print(f"  Interval: {INTERVAL_MENIT} menit")
    suhus = [r[1] for r in rows]; phs = [r[2] for r in rows]
    print(f"  Suhu    : {min(suhus):.1f} – {max(suhus):.1f} °C")
    print(f"  pH      : {min(phs):.2f} – {max(phs):.2f}")


if __name__ == "__main__":
    main()
