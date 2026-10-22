#!/usr/bin/env bash
# ==============================================================================
# install-lj1022-macos27.sh (v3.1 - Horizontale & Vertikale 1:1 Zentrierung)
# Korrigiert den +6.8 mm Rechtsversatz aus v3.0 (pad_left_bytes=0 & -78u20Z),
# sodass Links=Rechts und Oben=Unten in Word, Acrobat und PowerPoint exakt sitzen!
# ==============================================================================
set -euo pipefail

FILTER_NAME="rastertozj1022"
CUPS_FILTER_DIR="/usr/libexec/cups/filter"
PPD_DIR="/Library/Printers/PPDs/Contents/Resources"
PPD_FILENAME="HP-LaserJet-1022-macOS27.ppd"

echo "=== [1/5] Pruefe macOS Build-Umgebung & Apple Silicon Architektur ==="
ARCH=$(uname -m)
SW_VERS=$(sw_vers -productVersion || echo "macOS 27")
echo "Erkanntes System: macOS ${SW_VERS} (${ARCH})"

echo "=== [2/5] Kompiliere 64-Bit Universal Binary (arm64 + x86_64) v3.1 ==="
CFLAGS=$(cups-config --cflags 2>/dev/null || echo "")
LIBS=$(cups-config --image --libs 2>/dev/null || echo "-lcups -lcupsimage")

clang -O3 -arch arm64 -arch x86_64 -Wall -Wno-deprecated-declarations \
  ${CFLAGS} rastertozj1022.c ${LIBS} \
  -o "${FILTER_NAME}"

echo "=== [3/5] Entferne Gatekeeper-Quarantaene & signiere Binary (Ad-Hoc Mach-O) ==="
xattr -cr "${FILTER_NAME}"
codesign --force --sign - --timestamp=none "${FILTER_NAME}"

echo "=== [4/5] Installiere CUPS-Filter & PPD mit strikten root:wheel Rechten ==="
sudo mkdir -p "${CUPS_FILTER_DIR}"
sudo cp "${FILTER_NAME}" "${CUPS_FILTER_DIR}/${FILTER_NAME}"
sudo chown root:wheel "${CUPS_FILTER_DIR}/${FILTER_NAME}"
sudo chmod 0755 "${CUPS_FILTER_DIR}/${FILTER_NAME}"

sudo mkdir -p "${PPD_DIR}"
sudo cp "${PPD_FILENAME}" "${PPD_DIR}/${PPD_FILENAME}"
sudo chown root:wheel "${PPD_DIR}/${PPD_FILENAME}"
sudo chmod 0644 "${PPD_DIR}/${PPD_FILENAME}"

echo "=== [5/5] Konfiguriere DIN A4 als festes Standardformat & starte Kalibrierung ==="
USB_URI=$(lpinfo -v 2>/dev/null | grep -iE "usb://.*1022" | awk '{print $2}' | head -n 1 || true)
if [ -z "${USB_URI}" ]; then
  USB_URI="usb://Hewlett-Packard/HP%20LaserJet%201022?serial=JM1BN8J"
fi

cancel -a HP_LaserJet_1022 2>/dev/null || true
sudo lpadmin -p "HP_LaserJet_1022" \
  -E \
  -v "${USB_URI}" \
  -P "${PPD_DIR}/${PPD_FILENAME}" \
  -o PageSize=A4 \
  -o PageRegion=A4 \
  -o media=A4 \
  -D "HP LaserJet 1022" \
  -L "Franks MacBook Pro"

lpoptions -p "HP_LaserJet_1022" -o PageSize=A4 -o PageRegion=A4 -o media=A4 2>/dev/null || true
sudo cupsenable "HP_LaserJet_1022" || true
sudo cupsaccept "HP_LaserJet_1022" || true
sudo killall -HUP cupsd || true

echo "Sende 1-seitigen Hardware-Kalibrierungstest v3.1 an ${USB_URI} ..."
/usr/libexec/cups/filter/rastertozj1022 --self-test | lp -d HP_LaserJet_1022 -o raw
echo "Fertig! Horizontale (-6.8 mm Korrektur) und vertikale Zentrierung v3.1 ist aktiv."

