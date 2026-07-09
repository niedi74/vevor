# BLE-Protokoll: WT-0001 Kompressor-Kühlbox (Alpicool / Setpower / Vevor / BougeRV / ICECO)

> Status: **gegen echte Hardware verifiziert** (Gerät `WT-0001`, Firmware-Board wie in
> Alpicool-, Setpower-, Vevor-, BougeRV- und ICECO-Kühlboxen verbaut). Alle Prüfsummen
> und Feldbelegungen unten wurden anhand einer echten nRF-Connect-Aufzeichnung
> validiert (Checksummen 1:1 nachgerechnet, Feldwerte mit Screenshots der Original-App
> zeitlich korreliert). Einzige noch unbestätigte Angabe: das `On`-Bit (siehe
> Abschnitt „Offene Punkte“).

## Inhalt

- [Geräteerkennung](#geräteerkennung)
- [GATT-Profil](#gatt-profil)
- [Frame-Format & Prüfsumme](#frame-format--prüfsumme)
- [Befehle (App → Kühlbox)](#befehle-app--kühlbox)
- [Statusmeldungen (Kühlbox → App)](#statusmeldungen-kühlbox--app)
- [Settings-Byte-Tabelle](#settings-byte-tabelle)
- [Beispiel-Payloads](#beispiel-payloads)
- [Offene Punkte](#offene-punkte)
- [Referenzimplementierungen](#referenzimplementierungen)

## Geräteerkennung

Die Kühlbox bewirbt sich per BLE mit einem Namen, der mit einem dieser Präfixe
beginnt:

```
A1-...
AK1-...
AK2-...
AK3-...
```

Kein Pairing/Bonding nötig — direkt nach dem Scan verbinden. Es kann immer nur ein
BLE-Central gleichzeitig verbunden sein (Handy-App und eigenes Gerät gleichzeitig
funktioniert nicht).

## GATT-Profil

| Rolle | UUID | Eigenschaften |
|---|---|---|
| Service | `00001234-0000-1000-8000-00805f9b34fb` | — |
| Write-Charakteristik (Befehle senden) | `00001235-0000-1000-8000-00805f9b34fb` | **nur Write-Without-Response** (kein bestätigtes Write anfordern!) |
| Notify-Charakteristik (Status empfangen) | `00001236-0000-1000-8000-00805f9b34fb` | Notify + Read |
| Zusatz-Charakteristik (für Basissteuerung nicht nötig) | `00001237-0000-1000-8000-00805f9b34fb` | Notify + Write + Write-Without-Response |
| CCCD (Notify aktivieren) | `00002902-0000-1000-8000-00805f9b34fb` | auf `1236` |

Ablauf: verbinden → Service `1234` holen → Charakteristiken `1235` (write) und `1236`
(notify) holen → auf `1236` Notifications aktivieren → danach Befehle **ohne**
Response auf `1235` schreiben.

## Frame-Format & Prüfsumme

Jeder Frame (gesendete Befehle wie auch empfangene Statusmeldungen) hat denselben
Aufbau:

```
[Preamble: 2 Byte] [DataLen: 1 Byte] [CommandCode: 1 Byte] [Payload: N Byte] [Checksum: 2 Byte]
```

- **Preamble** immer `FE FE`.
- **DataLen** = `1 (CommandCode) + N (Payload) + 2 (Checksum)`.
- **Checksum**: einfache Byte-Summe aller vorangehenden Bytes (Preamble + DataLen +
  CommandCode + Payload), jedes Byte als unsigned 0–255 behandelt, auf 16 Bit
  gekürzt, **big-endian** gesendet (High-Byte zuerst). Keine echte CRC, nur eine
  simple Summe.

```
checksum = (Summe aller vorangehenden Bytes) & 0xFFFF
frame += [checksum >> 8, checksum & 0xFF]
```

⚠️ Auf Little-Endian-MCUs (z. B. ESP32): wird der Frame per `memcpy` aus einem C-Struct
gebaut, kommt das 2-Byte-Checksum-Feld in falscher Byte-Reihenfolge heraus — die
letzten beiden Bytes müssen vor dem Senden vertauscht werden.

## Befehle (App → Kühlbox)

### Ping / Keep-Alive (CommandCode `1`)

Statischer 6-Byte-Frame, ca. alle 1–2 Sekunden senden, um Verbindung zu halten und
eine Statusmeldung zu triggern:

```
FE FE 03 01 02 00
```

### Temperatur setzen (CommandCode `5`)

```
FE FE 04 05 <Temp> <ChecksumHi> <ChecksumLo>
```

- `Temp`: signed Byte (int8), Einheit (°C/°F) entsprechend dem aktuell eingestellten
  Modus (`CelsiusFahrenheitModeMenuE5`, siehe unten).

### Status/Settings setzen (CommandCode `2`)

Schreibt den **gesamten** 14-Byte-Settings-Block auf einmal — es gibt keinen
Einzel-Flag-Befehl. **Immer zuerst die letzte Statusmeldung auslesen, nur das
gewünschte Byte ändern und alles andere unverändert zurückschreiben.**

```
FE FE 11 02 <Settings: 14 Byte> <ChecksumHi> <ChecksumLo>
```

## Statusmeldungen (Kühlbox → App)

24-Byte-Frame, CommandCode `1`, wird als Antwort auf Ping sowie bei Änderungen
gesendet:

```
FE FE 15 01 <Settings: 14 Byte> <Sensors: 4 Byte> <ChecksumHi> <ChecksumLo>
```

**Sensors** (4 Byte):

| Byte | Feld | Bedeutung |
|---|---|---|
| 0 | `Temp` | Signed, aktuelle Temperatur (Einheit je nach `CelsiusFahrenheitModeMenuE5`) |
| 1 | `BatteryPercent` | Akku-Ladezustand in % (verifiziert: `0x64` = 100 %, deckt sich exakt mit Anzeige in der Original-App) |
| 2 | `InputV1` | Ganzzahliger Volt-Anteil der Eingangsspannung |
| 3 | `InputV2` | Zehntel-Volt-Anteil — tatsächliche Spannung = `InputV1 + InputV2/10` |

Zusätzlich: jeder auf `1235` geschriebene Befehl wird zuerst als kurzer Echo-Frame
(gleicher CommandCode, z. B. `05` für Temperatur setzen) über `1236` zurückgemeldet,
bevor eine vollständige 24-Byte-Statusmeldung (CommandCode `1`) mit dem neuen Zustand
folgt.

## Settings-Byte-Tabelle

Gilt identisch für den Settings-Block in „Status setzen“ (§ Befehle) und in den
Statusmeldungen.

| Byte | Feld | Bedeutung |
|---|---|---|
| 0 | `Locked` | `01` = Tastensperre aktiv, `00` = entsperrt |
| 1 | `On` | `01` = Kompressor/Kühlbox an, `00` = aus (⚠️ nicht gegen echte Hardware verifiziert, siehe unten) |
| 2 | `EcoMode` | `01` = ECO an, `00` = ECO aus — **verifiziert** (App-Screenshot mit ECO-Kachel aktiv deckte sich exakt mit Byte-Wert `01`) |
| 3 | `HLvl` | Batterie-/Unterspannungsschutz-Stufe — **verifiziert**: `0` = **L**, `1` = **M**, `2` = **H** (drei zeitlich zugeordnete Screenshots der Original-App bestätigen dies) |
| 4 | `TempSet` | Signed, gewünschte Solltemperatur |
| 5–6 | `E1`/`E2` (Thermostat-Grenzwerte) | Erweiterte Kalibrierung — **immer unverändert aus letzter Statusmeldung übernehmen**, nicht selbst berechnen |
| 7 | `HysteresisMenuE3` | Erweitert — unverändert übernehmen |
| 8 | `SoftStartDelayMinMenuE4` | Erweitert — unverändert übernehmen |
| 9 | `CelsiusFahrenheitModeMenuE5` | `01` = Fahrenheit, `00` = Celsius |
| 10–13 | `E6`/`E7`/`E8`/`E9` (Temperaturkompensation) | Erweitert — unverändert übernehmen |

## Beispiel-Payloads

Alle Beispiele wurden per Byte-Summe nachgerechnet und stimmen exakt:

| Beschreibung | Bytes |
|---|---|
| Ping | `FE FE 03 01 02 00` |
| Temperatur setzen auf 37 (0x25) | `FE FE 04 05 25 02 2A` |
| Temperatur setzen auf −2 (0xFE) | `FE FE 04 05 FE 03 03` |
| Status setzen (an, gesperrt, ECO an, HLvl=M, TempSet=0x43, …) | `FE FE 11 02 01 01 01 01 43 44 FC 04 00 01 00 00 FB 00 04 96` |
| Beispiel-Statusmeldung (aus Live-Capture) | `FE FE 15 01 00 01 01 00 02 08 F6 02 00 00 00 00 FD 00 01 64 0E 05 04 8B` |

## Offene Punkte

- **`On`-Byte (Ein-/Ausschalten):** in allen bisherigen Aufzeichnungen blieb dieser
  Wert konstant `01`. Semantik ist aus zwei unabhängigen Open-Source-Projekten
  übernommen, aber noch nicht per eigener Aufzeichnung bestätigt. Bestätigung: einmal
  während einer laufenden Aufzeichnung den Ein/Aus-Knopf betätigen und prüfen, ob das
  Byte zwischen `00`/`01` wechselt.
- **Charakteristik `1237`:** vorhanden, aber in keiner Aufzeichnung benötigt —
  vermutlich für eine hier nicht dokumentierte Zusatzfunktion (z. B. Dual-Zone oder
  erweiterte Konfiguration).
- **°C/°F-Umschaltung:** Byte 9 (`CelsiusFahrenheitModeMenuE5`) ist aus den
  Referenzprojekten übernommen, aber noch nicht in einer eigenen Aufzeichnung
  bestätigt.

## Referenzimplementierungen

- [johnelliott/alpicoold](https://github.com/johnelliott/alpicoold) — Go-Implementierung (HomeKit-Bridge)
- [jakub-hajek/alpicool-esp32-mqtt](https://github.com/jakub-hajek/alpicool-esp32-mqtt) — ESP32/PlatformIO-Implementierung mit MQTT/Home-Assistant-Anbindung
- Reverse-Engineering-Hintergrund: [johnelliott.org – Reverse Engineering a Bluetooth Fridge](https://johnelliott.org/blog/reverse-engineering-a-bluetooth-fridge/)
