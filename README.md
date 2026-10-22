# HP LaserJet 1022 – Native 64-Bit CUPS Driver for macOS (Apple Silicon)

Nativer, eigenständiger **64-Bit CUPS-Raster-zu-Zenographics-ZjStream-Treiber (`rastertozj1022`, v3.1)** und passendes **PPD** für den **HP LaserJet 1022 / 1022n / 1022nw** unter modernen macOS-Versionen (inkl. macOS Sequoia / macOS 26 / macOS 27).

## Hintergrund & Nachhaltigkeit (Gegen geplante Obsoleszenz)
Der HP LaserJet 1022 ist ein extrem langlebiger, mechanisch nahezu unverwüstlicher S/W-Laserdrucker mit **echter Hardware-PCL5e/ZjStream-Unterstützung** im ROM (er benötigt im Gegensatz zum LaserJet 1018/1020 keinen Firmware-Upload). Dennoch werden weltweit tausende funktionierende Geräte verschrottet, weil offizielle macOS-Treiberpakete für aktuelle 64-Bit/ARM64-Systeme fehlen.

Dieses Projekt stellt eine komplett freie, sauber kompilierbare **Public-Domain / MIT Open-Source-Implementierung** zur Verfügung:
- **Universal Binary (`arm64` + `x86_64`)**: Kompiliert direkt mit den macOS Command Line Tools (`clang -lcups -lcupsimage`).
- **Exakte DIN-A4- & US-Letter-Zentrierung (v3.1)**: Hardware-kalibrierte PCL-Registration (`\033&l-78u20Z`) und präzise `ImageableArea` für Word, Adobe Acrobat (PDF) und PowerPoint – egal ob *„An Papierformat anpassen“* ein- oder ausgeschaltet ist.
- **Direkte USB-Unterstützung**: Funktioniert sofort über das macOS-eigene `usb://`-CUPS-Backend.

## Schnellinstallation

1. Repository herunterladen oder klonen:
   ```bash
   git clone https://github.com/FrankThomsen-2026/hp-laserjet-1022-macos-driver.git
   cd hp-laserjet-1022-macos-driver

2. Installationsskript ausführen (kompiliert den Filter lokal und richtet den Drucker in CUPS ein):
   ```bash
   chmod +x install-lj1022-macos27.sh
   ./install-lj1022-macos27.sh

## Lizenz (Open Source — Public Good)

Veröffentlicht unter **MIT-0**. Frei nutzbar, veränderbar und weiterverbreitbar.
