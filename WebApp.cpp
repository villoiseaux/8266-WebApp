#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <Updater.h>
#include <time.h>

#define TRACES
#include "WebApp.h"

// Create AsyncWebServer object on port 80
AsyncWebServer appServer(80);
int _netActivityLed;                // the pin connected to LED indicating network activity
bool _netActivityLedLogic=true;    // false if the LED is ON on HIGH level, true if ON on LOW level.
size_t _contentLen;
String _appName;
String _appVersion;
String _buildNo;
bool _connectedToInternet;
timestatus _internetTime;

// Constructor(s)

WebApp::WebApp(String appName, String appversion){
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
    _appName=appName;
    _appVersion=appversion;
    _buildNo=String(__DATE__)+"@"+String(__TIME__);
    _connectedToInternet=false;
    _internetTime=NOT_CONNECTED;
} 
     
// destructor

    WebApp::~WebApp(){}

// publics methods
int WebApp::initWifi(){
  DEBUG("Connect to Wifi");
  WiFi.mode(WIFI_STA); 
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
    _connectedToInternet=true;
    return (true);
  } else {
    WiFi.mode(WIFI_AP); 
    boolean result = WiFi.softAP("ESPsoftAP_01", "pass-to-soft-AP");
    if(result == true){
      DEBUG("AP Ready");
      DEBUGVAL(WiFi.softAPIP());
      return (false);
    } else {
      DEBUG("AP Failed!");
    }    
  }
}

int WebApp::setTimeFromInternet(){
      _internetTime=IN_PROGRESS;
      DEBUG("Get internet time");
      if (!_connectedToInternet) {
        DEBUG("NOT CONNECTED");
        return (0);
      }
      configTime(0, 0, 
      "0.europe.pool.ntp.org",
      "1.europe.pool.ntp.org",
      "2.europe.pool.ntp.org");

      DEBUG("Ajust time");
      time_t now = time(0);
      while(now < 86400) {
        delay(1000);
        DEBUG("Ajusting in progress");
        now = time(0);
      }
      char timeBuffer[sizeof "0000-00-00T00:00:00Z"];
      strftime(timeBuffer, sizeof timeBuffer, "%FT%TZ", gmtime(&now));
      DEBUGVAL(timeBuffer);  
      _internetTime=CONNECTED;
      return (1);
}

int WebApp::initWebServer(){

  // For URL not registerd re-route to SPIFS static pages
  DEBUG ("Register hooks");
  appServer.on("/wlupdt", HTTP_POST, _wifiUpdate);

  appServer.on("/doUpdate", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                  size_t len, bool final) {_doUpdate(request, filename, index, data, len, final);}
  );
  // register API
  appServer.on("/api/getver", HTTP_GET, _api_getver);
  appServer.on("/api/gettime", HTTP_GET, _api_gettime);
  
  // ifnot found try static content or... 404
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
      AsyncWebServerResponse *r=request->beginResponse(SPIFFS, "/404.html", getContentType("/404.html"));
      r->setCode(404);
      request->send(r);
      logline+="404 ";
      //request->send(SPIFFS, "/404.html", getContentType("/404.html"));
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


void WebApp::_doUpdate(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index){
    Serial.println("Update");
    _contentLen = request->contentLength();
    // if filename includes spiffs, update the spiffs partition
    int cmd = U_FLASH;//(filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
#ifdef ESP8266
    Update.runAsync(true);
    if (!Update.begin(_contentLen, cmd)) {
#else
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
#endif
      Update.printError(Serial);
    }
  }

  if (Update.write(data, len) != len) {
    Update.printError(Serial);
#ifdef ESP8266
  } else {
    Serial.printf("Progress: %d%%\n", (Update.progress()*100)/Update.size());
#endif
  }

  if (final) {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the device reboots");
    response->addHeader("Refresh", "20");  
    response->addHeader("Location", "/");
    request->send(response);
    if (!Update.end(true)){
      Update.printError(Serial);
    } else {
      Serial.println("Update complete");
      Serial.flush();
      ESP.restart();
    }
  }
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

// API Methods

void WebApp::_api_getver (AsyncWebServerRequest *request){
  request->send(200, // OK
    "application/json", 
    String("{\"version\":\"")+ _appVersion +String("\",\"appid\":\""+_appName+"\",\"buildno\":\"" + _buildNo + "\"}")
    );
}


void WebApp::_api_gettime (AsyncWebServerRequest *request){
  time_t now = time(0);
  switch (_internetTime) {
    case (NOT_CONNECTED) : 
      request->send(200, // OK
        "application/json", 
        String("{\"timestamp\":\"")+ String(millis()/1000) + String("\",\"isodate\":\"NOT CONNECTED\",\"uptime\":\""+String(millis())+"\"}")
        );
      break;
  case (CONNECTED) : 
      char timeBuffer[sizeof "0000-00-00T00:00:00Z"];
      strftime(timeBuffer, sizeof timeBuffer, "%FT%TZ", gmtime(&now));      
      request->send(200, // OK
        "application/json", 
        String("{\"timestamp\":\"")+ String(now) + String("\",\"isodate\":\""+String(timeBuffer)+"\",\"uptime\":\""+String(millis())+"\"}")
        );
      break;
   case (IN_PROGRESS) :
      request->send(200, // OK
        "application/json", 
        String("{\"timestamp\":\"")+ String(millis()/1000) + String("\",\"isodate\":\"Connection in progress...\",\"uptime\":\""+String(millis())+"\"}")
        );
      break;
   default :
      request->send(200, // OK
        "application/json", 
        String("{\"timestamp\":\"")+ String(millis()/1000) + String("\",\"isodate\":\"TIME ERROR\",\"uptime\":\""+String(millis())+"\"}")
        );
   
  }

}
// Accessors
bool WebApp::isConnectedToInternet () {
  return _connectedToInternet;
}
