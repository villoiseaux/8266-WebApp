#define APP_VERSION "0.1.0"

// Import required libraries
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Updater.h>
#include <FS.h>

// Local Includes
#include "WebApp.h"

WebApp myTestApp("myTestApp","0.1.0");


void setup() {
  myTestApp.initWifi();
  myTestApp.initWebServer();
  myTestApp.run();
  myTestApp.setTimeFromInternet();
}

void loop() {
}
