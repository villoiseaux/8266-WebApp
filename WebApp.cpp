#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>

#define TRACES
#include "WebApp.h"

// Create AsyncWebServer object on port 80
AsyncWebServer appServer(80);
int _netActivityLed;                // the pin connected to LED indicating network activity
bool _netActivityLedLogic=true;    // false if the LED is ON on HIGH level, true if ON on LOW level.


// Constructor(s)

    WebApp::WebApp(){
      Serial.begin(115200);
      Serial.println("\n\r");
      DEBUG("Constructor");
      _netActivityLed=LED_BUILTIN;
      pinMode(_netActivityLed,OUTPUT);
      _setNetActifityLed(LOW);
      DEBUG("Open SPIFFS");
      if(!SPIFFS.begin()){
        FATAL("An Error has occurred while mounting SPIFFS");
        return;
      }
} 
     
// destructor

    WebApp::~WebApp(){}

// publics methods
int WebApp::initWifi(){
  DEBUG("Connect to Wifi");
  //WiFi.setmode(mode])
  WiFi.begin();  
  int loopcount=0;
  while (loopcount < 20 && WiFi.status() != WL_CONNECTED) {
    delay(500);
    _setNetActifityLed(HIGH);
    delay(500);
    _setNetActifityLed(LOW);
    loopcount++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG ("Wifi connected");
    DEBUGVAL (WiFi.localIP());
    return (true);
  } else {
    boolean result = WiFi.softAP("ESPsoftAP_01", "pass-to-soft-AP");
    if(result == true){
      DEBUG("AP Ready");
      DEBUGVAL(WiFi.softAPIP());
    } else {
      DEBUG("AP Failed!");
    }    
  }
}

int WebApp::initWebServer(){

  // For URL not registerd re-route to SPIFS static pages
  DEBUG ("Register hooks");
  appServer.on("/fwupdt", HTTP_POST, _fwUpdate);
  appServer.on("/wlupdt", HTTP_POST, _wifiUpdate);

  appServer.onNotFound( [](AsyncWebServerRequest *request){
    _setNetActifityLed(HIGH);
    String logline="##[STATIC] :";
    // Process URL
    String url=request->url();
    if (url=="/") url="/index.html"; // Autoindex
    url="/static"+url;
    
    AsyncClient *cli= request->client();
    IPAddress remote_host=cli->getRemoteAddress();

    logline+=remote_host.toString();
    logline+=" "+String(request->methodToString())+" "+url+" ";
    // IF FILE EXISTS IN SPIFFS
    if (_Fileexists(SPIFFS, url)) {
      logline+="200 ";
      request->send(SPIFFS, url, getContentType(url));
    } else {
    // OR THIS IS REALY A 404
      logline+="404 ";
      request->send(SPIFFS, "/404.html", getContentType("/404.html"));
    }
    // END OF LOGS
    if (request->hasHeader("User-Agent")){
      AsyncWebHeader* user_agent = request->getHeader("User-Agent");
      logline+=user_agent->value()+" ";
    } else {
      logline+="- ";
    }
    Serial.println(logline);
    _setNetActifityLed(LOW);
  });

  DEBUG ("BASIC WEB SERVER CONFIGURED");
  }

int WebApp::run(){
  appServer.begin();
  DEBUG ("WEB SERVER STARTED");
}

String WebApp::getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}


// PRIVATE METHODS

void WebApp::_setNetActifityLed(bool s){
  digitalWrite(_netActivityLed,s ^ _netActivityLedLogic);
}


bool WebApp::_Fileexists(FS &fs,const char* path){
  File f = fs.open(path, "r");
  return (f == true) && !f.isDirectory();
}


void WebApp::_fwUpdate (AsyncWebServerRequest *request){
  DEBUG("FIRMWARE UPDATE");
}

void WebApp::_wifiUpdate (AsyncWebServerRequest *request){
  DEBUG("WIFI UPDATE");
  String url="wifiupdate.html";
  DEBUGVAL(request->arg("ssid"));
  
  request->send(SPIFFS, "/wifiupdate.html", getContentType("/wifiupdate.html"));  

  unsigned long t=millis();
  ESP.wdtDisable();
  while (millis()<(t+2000)) {
   
  }
  WiFi.begin(request->arg("ssid"),request->arg("pwd"));  
}
