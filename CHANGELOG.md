# Changelog

## 1.1.1 – 2026-11-12

### Behoben
- Geändertes Ziel wurde mit HTML-Zeichen angezeigt (z.B. „<b>Wien Hbf (Bahns..“). Scotty markiert so ein vom Fahrplan abweichendes Ziel, etwa wenn ein Zug wegen einer Störung vorzeitig endet.

### Neu
- Ein geändertes Ziel wird jetzt **gelb mit „!“** angezeigt, in der Weboberfläche mit ⚠.
- Zusätze wie „(Bahnsteige 3-12)“ werden beim Ziel weggekürzt.

**Update:** In der Weboberfläche unter *System → Firmware-Update* die Datei `firmware-1.1.1-ota.bin` hochladen. Die Einstellungen bleiben erhalten.

## 1.1.0 – 2026-11-11

### Neu
- **Wiener Linien** als zweite Datenquelle (Echtzeit direkt von den Wiener Linien). Die Haltestellensuche ignoriert „Wien“, versteht „Hbf“ und ist bei Umlauten tolerant. U-Bahn-Linien erscheinen in ihren Linienfarben.
- **Linienfilter** pro Haltestelle (z.B. „S1, REX 7“ oder „U1, 13A“), kombinierbar mit dem Richtungsfilter.
- **Farbschema** pro Haltestelle: Standard, ÖBB-Blau, Wiener Linien (schwarz/grau) oder eigene Farben.
- **Wagenreihung**: Doppeltipp auf einen ÖBB-Fernverkehrszug zeigt Wagen, 1./2. Klasse, Business, Speisewagen, Zugteile mit Ziel, Rad-, Rollstuhl-, Ruhe- und Familienbereiche sowie die Auslastung (Daten: ÖBB Live). In der Weboberfläche gibt es dafür einen „Wagen“-Knopf.
- **Vorschau** pro Haltestelle: wie weit in die Zukunft Abfahrten angezeigt werden (Standard 60 Minuten, bis 24 Stunden).
- **Automatisches Weiterschalten** zwischen den Haltestellen (Standard alle 2 Minuten, abschaltbar).
- Dieselbe Haltestelle kann **mehrfach** angelegt werden, jeweils mit eigenen Filtern.
- Hinweis in der Weboberfläche, dass ein Firmware-Update ein paar Minuten dauern kann.

### Geändert
- **8 statt 7 Abfahrten**: Die Fußzeile ist weggefallen. Ein roter Punkt neben der Uhr zeigt, dass die letzte Abfrage fehlgeschlagen ist.
- Breitere Gleis-Spalte (z.B. „4D-F“), breitere Linien-Badges. Zugnummern werden nicht mehr abgeschnitten.
- Das **Abfrageintervall** (mindestens 20 Sekunden) wird jetzt immer eingehalten, auch beim Umschalten, bei „Jetzt abfragen“ und nach Fehlern.
- Abfahrten werden direkt aus der Verbindung gelesen: weniger Speicherbedarf, kleinere Anfragen an Scotty.
- Einstellungen werden in einem größeren Speicherbereich abgelegt. Bestehende Einstellungen werden beim Update übernommen.

### Behoben
- Doppelte Abfahrten bei Sammelhaltestellen (z.B. „Spittal/Drau“): Jede Fahrt erscheint nur noch einmal.
- „IncompleteInput“ bei kurz stockender Übertragung: Das Gerät wartet länger und versucht es einmal erneut.
- „TooDeep“ bei den Wiener Linien.
- Reste der Wagenreihungs-Ansicht am unteren Bildschirmrand.

**Update:** In der Weboberfläche unter *System → Firmware-Update* die Datei `firmware-1.1.0-ota.bin` hochladen. Die Einstellungen bleiben erhalten.

## 1.0.0 – 2026-10-27

Erste Version: Abfahrten aus ÖBB Scotty, bis zu 8 Haltestellen, Touch-Bedienung, Weboberfläche mit Haltestellensuche, OTA-Update, Nachtmodus, Installationsseite im Browser.
