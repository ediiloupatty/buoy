@echo off
REM Bebaskan port COM yang terkunci Serial Monitor / esptool macet.
REM Klik dua kali file ini kalau upload Arduino gagal "Access is denied".
powershell -NoProfile -Command "Get-Process | Where-Object { $_.ProcessName -match 'python3?$|esptool|serial-monitor' } | ForEach-Object { Write-Host ('Menutup: ' + $_.ProcessName + ' (PID ' + $_.Id + ')'); Stop-Process -Id $_.Id -Force }; Write-Host 'Selesai - coba upload lagi.'"
pause
