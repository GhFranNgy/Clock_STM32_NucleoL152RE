# STM32 Nucleo Clock, Stopwatch & Timer with Charging LED Effect

Ein vielseitiges Zeitmessungs-Projekt für das **NUCLEO-L152RE** Board mit einem **16x2 I2C LCD (PCF8574)** und einer optischen Lade-Animation (Charge-Effekt) über die GPIO-Ports C, B und A.

---

## 🚀 Features

* **Echtzeituhr & Kalender:** Zeigt Uhrzeit (`HH:MM:SS`) und Wochentag/Datum an (startet automatisch mit der Kompilierzeit).
* **Stoppuhr:** Millisekunden-genaue Zeitmessung mit Start-, Pause- und Reset-Funktion.
* **Countdown-Timer:** Einstellbarer Timer (Stunden & Minuten) mit automatischem Alarm („TIME'S UP!“) und blinkender Hintergrundbeleuchtung.
* **Lade-Lauflicht (Charge-Effekt):** Eine flüssige Non-Blocking-Animation, bei der sich die LEDs von unten nach oben (Port C -> B -> A) aufstapeln und oben angekommen zurücksetzen.
* **Interaktive Steuerung:** Pausiert das Lauflicht automatisch, wenn man zur normalen Uhrzeit (`PB0`) zurückkehrt.

---

## 📌 Hardware-Verkabelung

### LCD (PCF8574 I2C Backpack, Adresse `0x27`)
* **SDA** $\rightarrow$ D14 (PB9)
* **SCL** $\rightarrow$ D15 (PB8)

### Tasten (Mit `INPUT_PULLDOWN`, schalten nach HIGH)
* **PB0** $\rightarrow$ Zurück zur Uhrzeit (Pausiert das Lauflicht)
* **PB3** $\rightarrow$ Stoppuhr starten / Fortsetzen (Startet das Lauflicht)
* **PB4** $\rightarrow$ Stoppuhr stoppen / Zurücksetzen
* **PA6** $\rightarrow$ Countdown-Timer Modus / Nächstes Feld
* **PA10** $\rightarrow$ Countdown-Timer Wert erhöhen (+1, gedrückt halten zum schnellen Wiederholen)

---

## 🛠️ Technische Details

* **Non-Blocking Architektur:** Dank der Verwendung von `millis()` laufen das Lauflicht (50-ms-Takt) und die Uhrzeit komplett im Hintergrund, während Tastenabfragen verzögerungsfrei reagieren.
* **Direkte Register-Programmierung:** Die LED-Ports (`GPIOC`, `GPIOB`, `GPIOA`) werden über direkte Registerzugriffe (`MODER` und `ODR`) angesteuert, wobei die Pins `PA6` und `PA10` als Tasten-Inputs geschützt sind.

---

## 📦 Installation & Verwendung

1. Repository klonen oder den Code in **PlatformIO** oder die **Arduino IDE** (mit installiertem STM32 Cores) kopieren.
2. Sicherstellen, dass das Board **NUCLEO-L152RE** ausgewählt ist.
3. Den Code kompilieren und auf das Board flashen.
