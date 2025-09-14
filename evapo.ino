#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <time.h>

const char* ssid = "hoody";
const char* password = "Innov2299";
const char* apiKey = "53d59b13462b1991bb2e594c6d8cbb1d";

const float area_m2 = 10.0;
const float crop_coeff = 0.7;
const float flow_rate_mm_per_sec = 0.5;
const int valvePin = 5;

bool manualOverride = false;
bool valveState = false;

float lastET = 0.0;
float lastIrrigationTime = 0.0;
float currentTemp = 0.0;
float currentHumidity = 0.0;
String lastIrrigationTimestamp = "Never";

AsyncWebServer server(80);
unsigned long lastRun = 0;
const unsigned long interval = 24UL * 60UL * 60UL * 1000UL; // every 24 hours

String htmlPage() {
  String state = valveState ? "ON" : "OFF";
  String manual = manualOverride ? "ENABLED" : "DISABLED";

  return R"rawliteral(
    <!DOCTYPE html><html><head>
    <title>ET Irrigation Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body {
        font-family: 'Segoe UI', sans-serif;
        background: #f0f4f8;
        margin: 0;
        padding: 0;
      }
      .card {
        background: white;
        max-width: 600px;
        margin: 40px auto;
        padding: 20px;
        border-radius: 12px;
        box-shadow: 0 4px 12px rgba(0,0,0,0.1);
      }
      h1 {
        color: #00796b;
        text-align: center;
        margin-bottom: 10px;
      }
      .label {
        font-weight: bold;
        color: #555;
      }
      .value {
        font-size: 20px;
        margin-bottom: 10px;
        color: #333;
      }
      button {
        margin: 10px 5px;
        padding: 10px 20px;
        font-size: 16px;
        border: none;
        border-radius: 6px;
        cursor: pointer;
        color: white;
      }
      #on { background-color: #4caf50; }
      #off { background-color: #f44336; }
      #toggle { background-color: #03a9f4; }
      @media (max-width: 600px) {
        button { width: 100%; margin: 5px 0; }
      }
    </style>
    </head><body>
    <div class="card">
      <h1>ET-Based Irrigation</h1>
      <p><span class="label">Valve State:</span> <span class="value">)rawliteral" + state + R"rawliteral(</span></p>
      <p><span class="label">Manual Override:</span> <span class="value">)rawliteral" + manual + R"rawliteral(</span></p>
      <p><span class="label">Current Temperature:</span> <span id="temp" class="value">Loading...</span> &deg;C</p>
      <p><span class="label">Current Humidity:</span> <span id="humidity" class="value">Loading...</span> %</p>
      <p><span class="label">Last ET:</span> <span class="value">)rawliteral" + String(lastET, 2) + R"rawliteral( mm</span></p>
      <p><span class="label">Last Irrigation:</span> <span class="value">)rawliteral" + String(lastIrrigationTime, 2) + R"rawliteral( sec</span></p>
      <p><span class="label">Last Run:</span> <span class="value">)rawliteral" + lastIrrigationTimestamp + R"rawliteral(</span></p>
      <button id="on" onclick="location.href='/manualon'">Manual ON</button>
      <button id="off" onclick="location.href='/manualoff'">Manual OFF</button>
      <button id="toggle" onclick="location.href='/autotoggle'">Toggle Manual Mode</button>
    </div>
    <script>
      setInterval(() => {
        fetch("/weather").then(r => r.json()).then(data => {
          document.getElementById("temp").innerText = data.temp.toFixed(2);
          document.getElementById("humidity").innerText = data.humidity.toFixed(2);
        });
      }, 5000);
    </script>
    </body></html>
  )rawliteral";
}

void fetchAndIrrigate() {
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/onecall?lat=15.15&lon=120.58&appid=" + String(apiKey) + "&units=metric";
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      float Tmax = doc["daily"][0]["temp"]["max"];
      float Tmin = doc["daily"][0]["temp"]["min"];

      if (doc.containsKey("current")) {
        currentTemp = doc["current"]["temp"] | 0.0;
        currentHumidity = doc["current"]["humidity"] | 0.0;
      }

      float Tavg = (Tmax + Tmin) / 2;
      float Ra = 15.0;
      float ET0 = 0.0023 * (Tavg + 17.8) * sqrt(Tmax - Tmin) * Ra;
      float ET_mm = ET0 * 0.408;
      float irrigation_mm = ET_mm * crop_coeff;
      float valve_time_sec = (irrigation_mm * area_m2) / flow_rate_mm_per_sec;

      lastET = ET_mm;
      lastIrrigationTime = valve_time_sec;

      valveState = true;
      digitalWrite(valvePin, LOW); // ON (active LOW)
      delay((unsigned long)(valve_time_sec * 1000));
      digitalWrite(valvePin, HIGH); // OFF
      valveState = false;

      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        char buf[30];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        lastIrrigationTimestamp = String(buf);
      } else {
        lastIrrigationTimestamp = "Time error";
      }
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, HIGH); // OFF (active LOW)

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlPage());
  });

  server.on("/manualon", HTTP_GET, [](AsyncWebServerRequest *request){
    manualOverride = true;
    valveState = true;
    digitalWrite(valvePin, LOW); // ON
    request->redirect("/");
  });

  server.on("/manualoff", HTTP_GET, [](AsyncWebServerRequest *request){
    manualOverride = true;
    valveState = false;
    digitalWrite(valvePin, HIGH); // OFF
    request->redirect("/");
  });

  server.on("/autotoggle", HTTP_GET, [](AsyncWebServerRequest *request){
    manualOverride = !manualOverride;
    request->redirect("/");
  });

  server.on("/weather", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<128> doc;
    doc["temp"] = currentTemp;
    doc["humidity"] = currentHumidity;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  if (!manualOverride && millis() - lastRun >= interval) {
    lastRun = millis();
    fetchAndIrrigate();
  }
}
