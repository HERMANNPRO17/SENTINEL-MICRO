# SENTINEL MICRO V8.0 · Manuale / User guide

[Italiano](#italiano) · [English](#english) · [Presentazione / Presentation](SENTINEL_MICRO_V8.0_Presentation_IT-EN.pptx)

## Italiano

SENTINEL MICRO è un monitor ambientale per postazioni maker. Il BME280 legge temperatura, umidità e pressione. L’MQ-135 fornisce un indice relativo da 0 a 100, visualizzato anche dalla barra LED. L’indice non è una misura in ppm o un’indicazione di sicurezza dell’aria.

### Collegamenti

| Modulo | Alimentazione | Segnali ESP32-C3 |
|---|---|---|
| OLED SH1106 128 × 64 | 3,3 V e GND | SDA GPIO 8, SCL GPIO 9, indirizzo 0x3C |
| BME280 | 3,3 V e GND | SDA GPIO 8, SCL GPIO 9, indirizzo 0x76 o 0x77 |
| MQ-135 | 5 V e GND | A0 su GPIO 0 tramite partitore di tensione |
| 8 WS2812B | 5 V e GND | DIN GPIO 2 |

Collega tutte le masse. Verifica il partitore prima di collegare A0. La tensione sul GPIO 0 deve restare entro il limite ammesso dalla scheda.

### Caricamento

1. Installa **esp32 by Espressif Systems** nel Gestore schede e seleziona **ESP32C3 Dev Module**.
2. Installa **Adafruit GFX Library**, **Adafruit SH110X**, **Adafruit BME280 Library**, **Adafruit NeoPixel** e **ThingSpeak** dal Gestore librerie. Accetta le dipendenze Adafruit proposte.
3. Apri [SENTINEL_MICRO_V8_0.ino](SENTINEL_MICRO_V8_0/SENTINEL_MICRO_V8_0.ino). Seleziona la porta COM, abilita **USB CDC On Boot** e imposta **4 MB** sulle schede con questa capacità. Mantieni lo schema di partizione già usato se aggiorni. Per una prima installazione usa lo schema **Default 4MB with spiffs**, che mette a disposizione la partizione dati per LittleFS.
4. Per un aggiornamento lascia **Erase All Flash Before Sketch Upload** su **Disabled**. Verifica e carica. Se la porta manca, tieni premuto **BOOT**, premi e rilascia **RESET**, quindi rilascia **BOOT** e seleziona la nuova COM.

### Prima configurazione

Collegati alla rete **Sentinel-Micro** con la password iniziale **12345678**. Apri `http://192.168.4.1` e inserisci i dati della tua rete Wi-Fi. A connessione avvenuta, leggi l’IP sulla pagina **NETWORK / MEMORY**, poi aprilo da un telefono o PC sulla stessa rete. Usa la dashboard sulla rete locale. ThingSpeak è facoltativo e richiede ID canale e chiave di scrittura.

### Uso quotidiano

- **OLED.** La prima pagina mostra temperatura, umidità e indice aria. Le pagine successive mostrano pressione, quota, tendenza e stato di rete. **Falling** indica un calo della pressione.
- **Dashboard.** Consulta i grafici a 1, 6 e 24 ore. Imposta la quota reale per la correzione al livello del mare. Regola la luminosità della barra LED.
- **Calibrazione.** Applica gli scostamenti di temperatura e umidità dopo un confronto con uno strumento di riferimento. Per l’MQ-135, attendi la stabilizzazione e salva il riferimento in condizioni rappresentative.
- **Diagnosi.** Se l’OLED resta spento, controlla alimentazione, SDA, SCL e indirizzo I²C. Per i messaggi di avvio usa il monitor seriale a **115200 baud**. Se compare **LittleFS:ERR**, verifica lo schema di partizione.

## English

SENTINEL MICRO is an environmental monitor for maker workspaces. The BME280 reads temperature, humidity and pressure. The MQ-135 supplies a relative index from 0 to 100, also shown on the LED bar. The index does not measure ppm or establish whether the air is safe.

### Wiring

| Module | Power | ESP32-C3 signals |
|---|---|---|
| SH1106 128 × 64 OLED | 3.3 V and GND | SDA GPIO 8, SCL GPIO 9, address 0x3C |
| BME280 | 3.3 V and GND | SDA GPIO 8, SCL GPIO 9, address 0x76 or 0x77 |
| MQ-135 | 5 V and GND | A0 to GPIO 0 through a voltage divider |
| 8 WS2812B LEDs | 5 V and GND | DIN GPIO 2 |

Connect all grounds. Check the divider before connecting A0. Voltage at GPIO 0 must stay within the board’s input rating.

### Upload

1. Install **esp32 by Espressif Systems** in Boards Manager and select **ESP32C3 Dev Module**.
2. In Library Manager, install **Adafruit GFX Library**, **Adafruit SH110X**, **Adafruit BME280 Library**, **Adafruit NeoPixel** and **ThingSpeak**. Accept the suggested Adafruit dependencies.
3. Open [SENTINEL_MICRO_V8_0.ino](SENTINEL_MICRO_V8_0/SENTINEL_MICRO_V8_0.ino). Select the COM port, enable **USB CDC On Boot**, and select **4 MB** for boards with that capacity. Keep your existing partition scheme when updating. For a fresh installation, use **Default 4MB with spiffs**, which provides the data partition for LittleFS.
4. For an update, keep **Erase All Flash Before Sketch Upload** set to **Disabled**. Verify and upload. If the port is missing, hold **BOOT**, press and release **RESET**, then release **BOOT** and select the new COM port.

### First-time setup

Join **Sentinel-Micro** using the initial password **12345678**. Open `http://192.168.4.1` and enter your Wi-Fi credentials. After the device connects, read its IP on **NETWORK / MEMORY** and open it from a phone or PC on the same network. Use the dashboard on your local network. ThingSpeak is optional and requires a channel ID and write key.

### Everyday use

- **OLED.** The main page shows temperature, humidity and the air index. Other pages show pressure, altitude, trend and network status. **Falling** means pressure is decreasing.
- **Dashboard.** Open the 1-, 6- or 24-hour charts. Set your actual elevation for the sea-level pressure adjustment. Adjust LED brightness from the dashboard.
- **Calibration.** Compare temperature and humidity readings with a reference instrument before applying offsets. For the MQ-135, let it stabilize and save the baseline under representative conditions.
- **Troubleshooting.** If the OLED stays blank, check power, SDA, SCL and the I²C address. Set the Serial Monitor to **115200 baud** for startup messages. If you see **LittleFS:ERR**, check the partition scheme.
