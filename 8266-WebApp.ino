// Import required libraries
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

// Local Includes
#include "WebApp.h"

WebApp app;


void setup() {
 app.initWifi();
 app.initWebServer();
 app.run();
}

void loop() {
}
