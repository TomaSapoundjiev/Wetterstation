#define BLYNK_TEMPLATE_ID "TMPL4Xf52oeRd"
#define BLYNK_TEMPLATE_NAME "Wetterstation"
#define BLYNK_AUTH_TOKEN "TIsqI9EpyK9PMAupg-IHQQ67sxeN08Ml"

#include <BlynkSimpleEsp32.h>
#include "DHT.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <vector>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>


#define DHT11_PIN 2
DHT dht11(DHT11_PIN, DHT11);

#define I2C_SDA 7
#define I2C_SCL 3

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const int flameSensorPin = 4;
const int statusLedPin = 8;

WebServer server(80);
String webContent = "";
bool onlineModus = false;

struct Messwert {
  float temperatur;
  float feuchtigkeit;
  unsigned long timestamp;
};
std::vector<Messwert> history;

unsigned long lastLedToggle = 0;
bool ledState = false;
bool highTemp = false;

Adafruit_NeoPixel rgbLed(1, statusLedPin, NEO_GRB + NEO_KHZ800);

const char* discordWebhook = "https://discord.com/api/webhooks/1377064405900070912/CB9RCqTTlt8IhP5p_lVwCDnMWtbhHvK3w-3TNH_DBvy8XcI4b3ZnXIUBNk-0hhdQUuww";
unsigned long lastDiscordSend = 0;
const unsigned long discordInterval = 120000; // 2 Minuten

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  rgbLed.setPixelColor(0, rgbLed.Color(r, g, b));
  rgbLed.show();
}

void sendToDiscord(float temp, float hum, String flame) {
  if (!onlineModus) {
    Serial.println("❌ Nicht online – Discord-Sendung abgebrochen.");
    return;
  }

  Serial.println("🌐 Versuche Verbindung zu Discord Webhook...");
  HTTPClient http;

  if (!http.begin(discordWebhook)) {
    Serial.println("❌ Fehler beim Verbinden zu Discord.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  String payload = "{\"content\": \"🌤 **Wetterstation Update**\\n🌡 Temperatur: " + String(temp) + " °C\\n💧 Feuchtigkeit: " + String(hum) + " %\\n🔥 Flammenstatus: " + flame + "\"}";
  Serial.println("📤 Sende folgende Daten an Discord:");
  Serial.println(payload);

  int httpResponseCode = http.POST(payload);
  Serial.print("📡 HTTP Antwortcode: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    Serial.println("✅ Nachricht erfolgreich an Discord gesendet.");
    String response = http.getString();
    Serial.println(response);
  } else {
    Serial.print("❌ Fehler beim Senden an Discord: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}


void setup() {
  Serial.begin(9600);
  dht11.begin();
  Wire.begin(I2C_SDA, I2C_SCL);
  rgbLed.begin();
  rgbLed.show(); // LED aus

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 konnte nicht initialisiert werden"));
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Starte...");
  display.display();

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(35);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WLAN Setup...");
  display.println("AP: Wetterstation");
  display.display();

  if (wifiManager.autoConnect("Wetterstation", "wetter1234")) {
    Serial.println("WLAN verbunden!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WLAN verbunden!");
    display.display();
    onlineModus = true;

    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect();

    // Starteintrag für History
    Messwert startwert = {25.0, 50.0, millis() / 1000};
    history.push_back(startwert);

    // Erster Inhalt zur Anzeige setzen
    webContent = generateWebPage(startwert.temperatur, startwert.feuchtigkeit, "Unbekannt");

    // Webserver: Hauptseite
    server.on("/", []() {
      server.send(200, "text/html", webContent);
    });

    // Webserver: Daten-API für AJAX
    server.on("/data", []() {
      float temp = history.back().temperatur;
      float hum = history.back().feuchtigkeit;

      int sensorReading = analogRead(flameSensorPin);
      String flame;
      if (sensorReading > 3500) flame = "Sehr nah / stark";
      else if (sensorReading > 2000) flame = "Nahe Flamme";
      else if (sensorReading > 600) flame = "Entfernte Flamme";
      else flame = "Keine Flamme";

      String json = "{";
      json += "\"temperature\":" + String(temp) + ",";
      json += "\"humidity\":" + String(hum) + ",";
      json += "\"flame\":\"" + flame + "\"";
      json += "}";

      server.send(200, "application/json", json);
    });

    // Fehlerbehandlung für ungültige Routen
    server.onNotFound([]() {
      server.send(404, "text/plain", "Not found");
    });

    server.begin();
    Serial.println("Webserver gestartet");
  } else {
    Serial.println("Kein WLAN -> Offline-Modus");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Kein WLAN!");
    display.println("OFFLINE-MODUS");
    display.display();
    onlineModus = false;
  }
}


void updateStatusLed(float temp) {
  unsigned long now = millis();

  if (!onlineModus) {
    if (now - lastLedToggle >= 1000) {
      ledState = !ledState;
      setLedColor(ledState ? 255 : 0, 0, 0); // ROT blinkend
      lastLedToggle = now;
    }
    return;
  }

  if (temp > 25.0) {
    if (now - lastLedToggle >= 250) {
      ledState = !ledState;
      setLedColor(ledState ? 255 : 0, ledState ? 165 : 0, 0); // ORANGE blinkend
      lastLedToggle = now;
    }
    return;
  }

  setLedColor(0, 0, 255); // BLAU = Normalbetrieb
}

String generateWebPage(float temp, float hum, String flame) {
  float sum5 = 0, sum15 = 0, sum60 = 0;
  int count5 = 0, count15 = 0, count60 = 0;
  unsigned long now = millis() / 1000;

  String dataX = "", dataY_temp = "", dataY_hum = "";
  if (history.size() > 0) {
    int skip = history.size() > 20 ? history.size() / 20 : 1;
    for (int i = 0; i < history.size(); i += skip) {
      dataX += "'" + String(history[i].timestamp / 60) + "',";
      dataY_temp += String(history[i].temperatur) + ",";
      dataY_hum += String(history[i].feuchtigkeit) + ",";
    }
  }

  for (auto &m : history) {
    unsigned long ago = now - m.timestamp;
    if (ago <= 300) { sum5 += m.temperatur; count5++; }
    if (ago <= 900) { sum15 += m.temperatur; count15++; }
    if (ago <= 3600) { sum60 += m.temperatur; count60++; }
  }

  float avg5 = count5 ? sum5 / count5 : 0;
  float avg15 = count15 ? sum15 / count15 : 0;
  float avg60 = count60 ? sum60 / count60 : 0;

  String html = R"=====(<!DOCTYPE html><html><head>
  <meta charset='utf-8'>
  <title>Wetterstation</title>
  <style>
    body { font-family: sans-serif; margin: 20px; background: #f2f2f2; }
    h1 { color: #333; }
    .card { background: white; padding: 15px; border-radius: 8px; box-shadow: 0 2px 6px #ccc; margin-bottom: 20px; }
    canvas { width: 100%; max-width: 600px; height: 300px; }
  </style>
  <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
  <script>
    async function fetchLiveData() {
      try {
        const res = await fetch('/data');
        const data = await res.json();

        document.getElementById('temp').textContent = data.temperature + " °C";
        document.getElementById('hum').textContent = data.humidity + " %";
        document.getElementById('flame').textContent = data.flame;
      } catch (e) {
        console.error("Fehler beim Laden der Daten:", e);
      }
    }

    setInterval(fetchLiveData, 5000);
    window.onload = fetchLiveData;
  </script>

</head><body>)=====";

  html += "<h1>🌦 Wetterstation</h1>";
  html += "<div class='card'><p><strong>Aktuelle Temperatur:</strong> <span id='temp'>" + String(temp) + " °C</span></p>";
  html += "<p><strong>Feuchtigkeit:</strong> <span id='hum'>" + String(hum) + " %</span></p>";
  html += "<p><strong>Flammenstatus:</strong> <span id='flame'>" + flame + "</span></p></div>";

  html += "<div class='card'><h3>Durchschnittswerte</h3>";
  html += "<p>Letzte 5 min: " + String(avg5, 1) + " &deg;C</p>";
  html += "<p>Letzte 15 min: " + String(avg15, 1) + " &deg;C</p>";
  html += "<p>Letzte 60 min: " + String(avg60, 1) + " &deg;C</p></div>";

  html += "<div class='card'><h3>Temperaturverlauf</h3><canvas id='chartTemp'></canvas></div>";
  html += "<div class='card'><h3>Feuchtigkeitsverlauf</h3><canvas id='chartHum'></canvas></div>";

  html += "<script>const ctxT = document.getElementById('chartTemp').getContext('2d');";
  html += "const chartT = new Chart(ctxT, { type: 'line', data: { labels: [" + dataX + "], datasets: [{ label: 'Temperatur (°C)', data: [" + dataY_temp + "], borderColor: 'orange', tension: 0.2, fill: false }] }, options: { responsive: true, scales: { x: { title: { display: true, text: 'Minuten' } }, y: { title: { display: true, text: '°C' } } } } });";
  html += "const ctxH = document.getElementById('chartHum').getContext('2d');";
  html += "const chartH = new Chart(ctxH, { type: 'line', data: { labels: [" + dataX + "], datasets: [{ label: 'Feuchtigkeit (%)', data: [" + dataY_hum + "], borderColor: 'blue', tension: 0.2, fill: false }] }, options: { responsive: true, scales: { x: { title: { display: true, text: 'Minuten' } }, y: { title: { display: true, text: '%' } } } } });</script>";

  html += "</body></html>";
  return html;
}

void loop() {
  Blynk.run();
  delay(2000);
  float humi = dht11.readHumidity();
  float tempC = dht11.readTemperature();
  int sensorReading = analogRead(flameSensorPin);

  String flameStatus;
  if (sensorReading > 3500) {
    flameStatus = "Sehr nah / stark";
  } else if (sensorReading > 2000) {
    flameStatus = "Nahe Flamme";
  } else if (sensorReading > 600) {
    flameStatus = "Entfernte Flamme";
  } else {
    flameStatus = "Keine Flamme";
  }

  int flamePercent = map(sensorReading, 0, 1023, 0, 100);
  flamePercent = constrain(flamePercent, 0, 100);

  if (!isnan(tempC) && !isnan(humi)) {
    Messwert m = { tempC, humi, millis() / 1000 };
    history.push_back(m);
    if (history.size() > 200) history.erase(history.begin());
  }

  if (!isnan(humi) && !isnan(tempC) && onlineModus) {
    webContent = generateWebPage(tempC, humi, flameStatus);
    Blynk.virtualWrite(V0, tempC);
    Blynk.virtualWrite(V1, humi);
    Blynk.virtualWrite(V2, flamePercent);
  }

  Serial.print("Temp: "); Serial.print(tempC);
  Serial.print(" C | Feucht: "); Serial.print(humi);
  Serial.print(" % | Flamme: "); Serial.print(flameStatus);
  Serial.print(" | Index: "); Serial.println(sensorReading);

  display.clearDisplay();
  display.setCursor(0, 0);
  if (isnan(humi) || isnan(tempC)) {
    display.println("Sensorfehler!");
  } else {
    display.print("Temp: "); display.print(tempC); display.println(" C");
    display.print("Feucht: "); display.print(humi); display.println(" %");
    display.print("Flamme: "); display.println(flameStatus);
  }
  display.display();

  updateStatusLed(tempC);

  if (onlineModus) {
    server.handleClient();
  }
  if (millis() - lastDiscordSend >= discordInterval && !isnan(tempC) && !isnan(humi)) {
    sendToDiscord(tempC, humi, flameStatus);
    lastDiscordSend = millis();
  }
  
}
