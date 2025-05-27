#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#define BUTTON_PIN 0 // Вбудована кнопка
#define EEPROM_SIZE 8

// WiFi та сервер
const char *ssidPattern = "ExpressLRS RX";
const char *serverUrlOptions = "http://10.0.0.1/options.json";
const char *serverUrlConfig = "http://10.0.0.1/config";
const char *serverUrlReboot = "http://10.0.0.1/reboot";
const char *wifiPassword = "expresslrs";

// WiFi AP настройки
const char *ap_ssid = "ESP8266-AP";   // SSID точки доступу
const char *ap_password = "12345678"; // Пароль точки доступу

unsigned long buttonPressTime = 0;
bool buttonPressed = false;
bool inBindMode = true;
const unsigned long longPressDuration = 1000; // 1 секунда
ESP8266WebServer server(80);                  // Веб-сервер на порту 80
String bindPhrase;                            // Фраза прив'язки
bool ELRS_version = true; // Версія ELRS (true - 3.5+, false - 2.0+)
bool startProcess = false;

const String postBodyTemplate = R"({
  "flash-discriminator": 3347712067,
  "uid": [80, 110, 171, 188, 48, 119],
  "domain": 1,
  "wifi-on-interval": 60,
  "rcvr-uart-baud": 420000,
  "lock-on-first-connection": true,
  "is-airport": false,
  "customised": true,
  "bind-phrase": ""})";

// Завантажуємо фразу прив'язки з EEPROM
void loadBindPhrase() {
  EEPROM.begin(EEPROM_SIZE);
  uint8_t storedPhrase[EEPROM_SIZE];
  bool storedELRS_version;
  EEPROM.get(0, storedPhrase[0]);
  EEPROM.get(1, storedPhrase[1]);
  EEPROM.get(2, storedPhrase[2]);
  EEPROM.get(3, storedPhrase[3]);
  EEPROM.get(4, storedPhrase[4]);
  EEPROM.get(5, storedPhrase[5]);
  EEPROM.get(6, storedELRS_version);

  bindPhrase = {String(storedPhrase[0]) + ',' + String(storedPhrase[1]) + ',' +
                String(storedPhrase[2]) + ',' + String(storedPhrase[3]) + ',' +
                String(storedPhrase[4]) + ',' + String(storedPhrase[5])};
  if (storedELRS_version == false) {
    ELRS_version = false;
  } else {
    ELRS_version = true;
  }

  if (bindPhrase == "")
    bindPhrase = "1,1,1,1,1,1"; // Значення за замовчуванням
  EEPROM.end();
  Serial.println("Loaded bind phrase: " + bindPhrase);
}

// Зберігаємо нову фразу прив'язки в EEPROM
void saveBindPhrase(String newPhrase[6]) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, (uint8_t)newPhrase[0].toInt());
  EEPROM.put(1, (uint8_t)newPhrase[1].toInt());
  EEPROM.put(2, (uint8_t)newPhrase[2].toInt());
  EEPROM.put(3, (uint8_t)newPhrase[3].toInt());
  EEPROM.put(4, (uint8_t)newPhrase[4].toInt());
  EEPROM.put(5, (uint8_t)newPhrase[5].toInt());
  EEPROM.commit();
  EEPROM.end();
  bindPhrase = String(newPhrase[0]) + "," + String(newPhrase[1]) + "," +
               String(newPhrase[2]) + "," + String(newPhrase[3]) + "," +
               String(newPhrase[4]) + "," + String(newPhrase[5]);
  Serial.println("Saved new bind phrase: " + bindPhrase);
}

void saveELRSVersion(bool version) {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(6, version);
  EEPROM.commit();
  EEPROM.end();
  ELRS_version = version;
  Serial.println("Saved ELRS version: " + String(version));
}

// Надсилаємо POST запит на сервер
void sendPostRequest(const char *serverUrl) {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "application/json");

  String postBody = postBodyTemplate;

  String generatedUID = "[" + bindPhrase + "]";

  // Заміна тільки значення для "uid"
  postBody.replace("uid\": [80, 110, 171, 188, 48, 119]",
                   "uid\": " + generatedUID);

  int httpResponseCode = http.POST(postBody);
  Serial.print("POST sent. Code: ");
  Serial.println(httpResponseCode);
  http.end();
}

// Скануємо доступні мережі та підключаємося до кращої
void scanAndConnect() {

  Serial.println("Switching to STA mode...");
  WiFi.mode(WIFI_STA);

  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  Serial.println("Scan complete!");

  String bestSSID = "";
  int bestRSSI = -100;

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    Serial.printf("Found network: %s, RSSI: %d\n", ssid.c_str(), rssi);

    if (ssid.indexOf(ssidPattern) >= 0 && rssi > bestRSSI) {
      bestRSSI = rssi;
      bestSSID = ssid;
    }
  }

  if (bestSSID != "") {
    Serial.printf("Connecting to %s...\n", bestSSID.c_str());
    WiFi.begin(bestSSID.c_str(), wifiPassword);
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - startAttemptTime >= 30000) {
        Serial.println("Failed to connect.");
        return;
      }
      delay(500);
    }

    Serial.println("Connected!");
    if (ELRS_version) {
      Serial.println("ELRS version: 3.5+");
      sendPostRequest(serverUrlConfig);
    } else {
      Serial.println("ELRS version: 2.0+");
      sendPostRequest(serverUrlOptions);
    }
    sendPostRequest(serverUrlReboot);

  } else {
    Serial.println("No suitable network found.");
  }
}

// Веб-сторінка для налаштування фрази прив'язки
void handleRoot() {
  String html = "<html><body><h1>ELRS Config</h1>";
  html += "<form action='/set' method='POST'>";
  html += "Bind Phrase 1 byte: <input type='text' name='phrase1'><br>";
  html += "Bind Phrase 2 byte: <input type='text' name='phrase2'><br>";
  html += "Bind Phrase 3 byte: <input type='text' name='phrase3'><br>";
  html += "Bind Phrase 4 byte: <input type='text' name='phrase4'><br>";
  html += "Bind Phrase 5 byte: <input type='text' name='phrase5'><br>";
  html += "Bind Phrase 6 byte: <input type='text' name='phrase6'><br>";
  html += "ELRS Version: <select name='elrs_version'>";
  html += "<option value='true'" + String(ELRS_version ? " selected" : "") +
          ">3.5+</option>";
  html += "<option value='false'" + String(ELRS_version ? "" : " selected") +
          ">2.0+</option>";
  html += "</select><br>";
  html += "<input type='submit' value='Save'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

// Обробка POST запиту для збереження фрази прив'язки
void handleSet() {
  if (server.hasArg("phrase1")) {
    String fullPhrase[6] = {server.arg("phrase1"), server.arg("phrase2"),
                            server.arg("phrase3"), server.arg("phrase4"),
                            server.arg("phrase5"), server.arg("phrase6")};
    saveBindPhrase(fullPhrase);
  }
  if (server.hasArg("elrs_version")) {
    bool version = server.arg("elrs_version") == "true";
    saveELRSVersion(version);
  }
  server.send(200, "text/html", "<h1>Saved!<br><a href='/'>Back</a></h1>");
}

void setup() {
  Serial.begin(9600);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  loadBindPhrase();

  // Початково запускаємо точку доступу
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/set", HTTP_POST, handleSet);
  server.begin();
}

void loop() {
  static bool buttonState = HIGH;
  static bool lastButtonState = HIGH;

  buttonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && buttonState == LOW) {
    buttonPressTime = millis();
    buttonPressed = true;
  } else if (lastButtonState == LOW && buttonState == HIGH && buttonPressed) {
    unsigned long pressDuration = millis() - buttonPressTime;
    buttonPressed = false;

    if (pressDuration >= longPressDuration) {
      // Довге натискання
      inBindMode = !inBindMode; // Перемикання режиму
      Serial.println(inBindMode ? "Entering SETTINGS mode"
                                : "Exiting SETTINGS mode");
      if (inBindMode) {
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP(ap_ssid, ap_password);
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
      }
    } else {
      // Коротке натискання
      int x = 0;
      if (!inBindMode) {
        while (x < 3) {
          scanAndConnect();
          x++;
        }
      } else {
        Serial.println("Short press ignored in SETTINGS mode");
      }
    }
  }

  lastButtonState = buttonState;

  if (inBindMode) {
    server.handleClient();
  }
}
