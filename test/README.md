# Host-Tests der Datenauswertung

Prüft Scotty-, Wiener-Linien- und ÖBB-Live-Auswertung (`src/hafas.cpp`) am PC mit Beispieldaten
in `test/data/` (Struktur wie die echten Antworten). Die Arduino-Funktionen werden durch `test/shim/` ersetzt.

```bash
# einmal "pio run" ausführen, dann liegt ArduinoJson unter .pio/libdeps/cyd/ArduinoJson
g++ -std=gnu++17 -DARDUINO=10819 -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 \
    -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1 -DARDUINOJSON_ENABLE_PROGMEM=0 \
    -Itest/shim -I.pio/libdeps/cyd/ArduinoJson/src -o test_parser test/test_parser.cpp src/hafas.cpp
./test_parser
```
