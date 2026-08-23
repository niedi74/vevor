# vevor

ESP32-Steuerung für Vevor / Setpower / Alpicool-kompatible Kompressor-Kühlboxen
(BLE-Board `WT-0001`) — reverse-engineeredes BLE-Protokoll plus fertiger
ESP32-Sketch zur direkten Ansteuerung ohne die Original-App.

## Inhalt

- [`docs/FRIDGE_BLE_PROTOCOL.md`](docs/FRIDGE_BLE_PROTOCOL.md) — vollständige
  Protokoll-Referenz (GATT-UUIDs, Frame-Format, Prüfsumme, alle Befehle,
  Settings-Byte-Tabelle). Gegen echte Hardware verifiziert.
- [`src/esp32_fridge_control.ino`](src/esp32_fridge_control.ino) — eigenständiger
  Arduino-Sketch (NimBLE-Arduino) zum Scannen, Verbinden, Statusauslesen und
  Steuern (Ein/Aus, ECO, Sperre, Batterieschutz-Stufe, Solltemperatur) über die
  serielle Konsole.
- [`src/esp32_touch_panel/`](src/esp32_touch_panel/) — Touch-Bedienpanel
  (PlatformIO, Waveshare ESP32-S3-LCD-1.54", LVGL) als Ersatz für die
  Handy-App bzw. eine unzugängliche Original-Bedieneinheit: Ein/Aus,
  Solltemperatur, Ist-Temperatur, Batterie/Spannung, Verbindungsstatus.
  Details siehe [`src/esp32_touch_panel/CODE.md`](src/esp32_touch_panel/CODE.md).

## Kompatible Geräte

Kühlboxen mit BLE-Advertising-Name `A1-...`, `AK1-...`, `AK2-...` oder `AK3-...`
— verkauft u. a. unter den Marken Vevor, Setpower, Alpicool, BougeRV, ICECO.
Getestet gegen ein Gerät `WT-0001`.

## Schnellstart

1. Arduino IDE oder PlatformIO mit ESP32-Boardunterstützung einrichten.
2. Bibliothek **NimBLE-Arduino** installieren.
3. `src/esp32_fridge_control.ino` öffnen, ggf. `FRIDGE_BLE_ADDRESS` eintragen
   (oder Namenspräfix-Autodetect nutzen, siehe Kommentare im Sketch).
4. Flashen, seriellen Monitor auf 115200 Baud öffnen.
5. Befehle: `on`, `off`, `eco on`, `eco off`, `lock on`, `lock off`,
   `temp <n>`, `status`.

Für das Touch-Panel stattdessen: `cd src/esp32_touch_panel && pio run -t upload
-t monitor`. Vorher die Pins in `include/display_config.h` gegen das eigene
Board prüfen und den Touch-Treiber-Stub in `src/main.cpp` implementieren
(siehe „Offene Punkte“ in `CODE.md`).

## Status / offene Punkte

Details siehe [`docs/FRIDGE_BLE_PROTOCOL.md`](docs/FRIDGE_BLE_PROTOCOL.md#offene-punkte).
Kurzfassung: Sperre, ECO, Batterieschutz-Stufe (L/M/H) und Solltemperatur sind
gegen echte Hardware verifiziert. Das reine Ein/Aus-Bit ist bisher nur aus
öffentlichen Referenzprojekten übernommen, noch nicht per eigener Aufzeichnung
bestätigt.

## Credits

Basiert auf Reverse-Engineering-Arbeit von:

- [johnelliott/alpicoold](https://github.com/johnelliott/alpicoold)
- [jakub-hajek/alpicool-esp32-mqtt](https://github.com/jakub-hajek/alpicool-esp32-mqtt)

## Lizenz

MIT, siehe [LICENSE](LICENSE).
