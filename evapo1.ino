#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

const char* ssid = "hoody";
const char* password = "Innov2299";
const char* apiKey = "53d59b13462b1991bb2e594c6d8cbb1d ";
const float area_m2 = 10.0;
const float crop_coeff = 0.7;
const float flow_rate_mm_per_sec = 0.5;

const int valvePin = 5;
bool manualOverride = false;
bool valveState = false;

AsyncWebServer server(80);

String htmlPage() {
  String state = valveState ? "ON" : "OFF";
  String manual = manualOverride ? "ENABLED" : "DISABLED";
  return "<!DOCTYPE html><html><head><title>ET Monitor</title><style>"
         "body { font-family: sans-serif; text-align: center; background: #e0f7fa; padding: 50px; }"
         "h1 { color: #00796b; }"
         "button { padding: 10px 20px; font-size: 18px; margin: 10px; border: none; border-radius: 5px; cursor: pointer; }"
         "#on { background: #4caf50; color: white; }"
         "#off { background: #f44336; color: white; }"
         "#toggle { background: #03a9f4; color: white; }"
         "</style></head><body><h1>Evapotranspiration Monitor</h1>"
         "<p>Valve State: <strong>" + state + "</strong></p>"
         "<p>Manual Override: <strong>" + manual + "</strong></p>"
         "<button id='on' onclick=\"location.href='/manualon'\">Manual ON</button>"
         "<button id='off' onclick=\"location.href='/manualoff'\">Manual OFF</button>"
         "<button id='toggle' onclick=\"location.href='/autotoggle'\">Toggle Manual Mode</button>"
         "</body></html>";
}

void setup() {
  Serial.begin(115200);
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlPage());
  });

  server.on("/manualon", HTTP_GET, [](AsyncWebServerRequest *request){
    manualOverride = true;
    valveState = true;
    digitalWrite(valvePin, HIGH);
    request->redirect("/");
  });

  server.on("/manualoff", HTTP_GET, [](AsyncWebServerRequest *request){
    manualOverride = true;
    valveState = false;
    digitalWrite(valvePin, LOW);
    request->redirect("/");
  });

  server.on("/autotoggle", HTTP_GET, [](AsyncWebServerRequest *request){
    manualOverride = !manualOverride;
    request->redirect("/");
  });

  server.begin();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && !manualOverride) {
    HTTPClient http;
    http.begin("http://api.openweathermap.org/data/2.5/onecall?lat=15.15&lon=120.58&appid=" + String(apiKey) + "&units=metric");
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(4096);
      deserializeJson(doc, payload);

      float Tmax = doc["daily"][0]["temp"]["max"];
      float Tmin = doc["daily"][0]["temp"]["min"];
      float Tavg = (Tmax + Tmin) / 2;
      float Ra = 15; // estimated
      float ET0 = 0.0023 * (Tavg + 17.8) * sqrt(Tmax - Tmin) * Ra;
      float ET_mm = ET0 * 0.408;
      float irrigation_mm = ET_mm * crop_coeff;
      float valve_time_sec = (irrigation_mm * area_m2) / flow_rate_mm_per_sec;

      digitalWrite(valvePin, HIGH);
      delay((unsigned long)(valve_time_sec * 1000));
      digitalWrite(valvePin, LOW);
    }
    http.end();
  }
  delay(86400000); // once per day
}
