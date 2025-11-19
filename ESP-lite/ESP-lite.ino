#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>

const char* ssid = "Titenet-IoT";         // Wi-Fi network name
const char* password = "7kDtaphg";        // Wi-Fi password

ESP8266WebServer server(80);              // Web server on port 80

void setup() {
  Serial.begin(115200);        // Для отладки через USB
  Serial1.begin(9600);         // Serial1 для связи с Arduino Mega

  // Инициализация SPIFFS
  if (!SPIFFS.begin()) {
    Serial.println("Error mounting SPIFFS");
    return;
  }

  // Подключение к Wi-Fi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // Настройка статических файлов веб-сайта
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/style.css", SPIFFS, "/style.css");
  server.serveStatic("/script.js", SPIFFS, "/script.js");
  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.png");

  // URL команды движения
  server.on("/forwards5", [](){ handleMove(5); });
  server.on("/forwards20", [](){ handleMove(20); });
  server.on("/backwards5", [](){ handleMove(-5); });
  server.on("/backwards20", [](){ handleMove(-20); });

  // URL команды компаса
  server.on("/compass", handleCompass);
 

  // Если URL не найден
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();  // Обработка запросов
}

// Обработка несуществующих URL
void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

// Обработка команд движения
void handleMove(int distance) {
  String cmd = "dist:" + String(distance);
  Serial.println(cmd);
  server.send(200, "text/plain", "OK");
}



// Обработка команды компасатшщк
void handleCompass() {
  if (server.hasArg("value")) {
    String valueString = server.arg("value");
    String cmd = "deg:" + valueString;
    Serial.println(cmd);

  }
  server.send(200, "text/plain", "OK");
}
