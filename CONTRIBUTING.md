# Mitmachen

Danke, dass du helfen willst! Ein paar Hinweise, damit es für alle einfach bleibt.

## Fehler melden

Bitte ein [Issue](../../issues/new/choose) mit der Vorlage „Fehlerbericht“ anlegen und angeben:

- Firmware-Version (Weboberfläche → System, oder lange aufs Display drücken)
- Display-Variante (Foto der Rückseite hilft)
- betroffene Haltestelle
- bei Problemen mit Abfahrten: Inhalt von *System → Debug: Rohdaten*

## Code beitragen

1. Repository forken und einen Branch anlegen (`git checkout -b mein-feature`).
2. Mit [PlatformIO](https://platformio.org) bauen und am echten Gerät testen: `pio run -t upload`.
3. Pull Request mit kurzer Beschreibung öffnen: was geändert wurde und wie du es getestet hast.

Jeder Pull Request wird automatisch gebaut (GitHub Actions). Er sollte ohne Fehler kompilieren.

### Richtlinien

- Kommentare und Oberflächentexte auf Deutsch, Code-Bezeichner auf Englisch
- Kleine, fokussierte Pull Requests sind leichter zu prüfen als große
- Speicher ist knapp (ESP32 ohne PSRAM): große Puffer statisch anlegen, JSON mit Filter parsen
- Neue Einstellungen in `config.cpp` mit Standardwert versehen, damit bestehende Geräte nach dem Update weiterlaufen
- Bitte die Scotty-Server schonen: kein Abfrageintervall unter 20 s

## Neue Version veröffentlichen (für Maintainer)

```bash
git tag v1.1.0
git push origin v1.1.0
```

GitHub Actions baut dann die Firmware, legt ein Release mit den `.bin`-Dateien an und
aktualisiert die Installationsseite auf GitHub Pages.
