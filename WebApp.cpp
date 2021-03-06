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

// Global variables in order to be used in static methods.
// This is necessary for call back based HTTP server

int _netActivityLed;                // the pin connected to LED indicating network activity
bool _netActivityLedLogic=true;    // false if the LED is ON on HIGH level, true if ON on LOW level.
namespace buttonMgt {
  enum  buttonStatus {up=false, down=true};
  int _resetButtonPin=0;
  bool _resetButtonLogic=false;      // signal low when button pushed
}
size_t _contentLen;
String _appName;
String _appVersion;
String _buildNo;
String _ap_ssid;
String _ap_key;
bool _connectedToInternet;
timestatus _internetTime;
unsigned updateProgress;


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
    _ap_ssid=_appName;
    _ap_key="";
} 
     
// destructor

    WebApp::~WebApp(){}

// publics methods

int WebApp::setAPCreds(String ssid, String key){
  _ap_ssid=ssid;
  _ap_key=key;
}

int WebApp::initWifi(){
  DEBUG("Connect to Wifi");
  WiFi.mode(WIFI_STA); 
  WiFi.begin();  
  int loopcount=0;
  unsigned long m=millis();
  while (loopcount < 20 && WiFi.status() != WL_CONNECTED && WiFi.status() != WL_NO_SSID_AVAIL) {
    delay (100);
    if ((millis()%1000)<500){
      _setNetActifityLed(HIGH);
    } else {
      _setNetActifityLed(LOW);
    }
    if (_getButtonStatus()==false){
      DEBUG("FORCE WIFI RESET");
      loopcount=-1;
      break;
    }   
    if (millis()>(m+1000)) {
      loopcount++;
      DEBUGVAL(WlStatusToStr(WiFi.status()));
      m=millis();
    }
  }
  if ((loopcount!=-1) && (WiFi.status() == WL_CONNECTED)) {
    _setNetActifityLed(LOW);
    DEBUG ("Wifi connected");
    DEBUGVAL (WiFi.localIP());
    _connectedToInternet=true;
    return (true);
  } else {
    WiFi.mode(WIFI_AP); 
    _setNetActifityLed(HIGH);
    
    boolean result = WiFi.softAP(_ap_ssid,_ap_key);
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
        delay(980);
        _setNetActifityLed(HIGH);
        delay(20);
        _setNetActifityLed(LOW);
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
  appServer.on("/api/netlist", HTTP_GET, _api_get_ssid_list);
  
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
  _scanAndSort();
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

bool WebApp::_getButtonStatus(){
  return ((digitalRead(buttonMgt::_resetButtonPin)^buttonMgt::_resetButtonLogic)==true); 
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
    updateProgress=0;
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
    updateProgress=(Update.progress()*100)/Update.size();
    Serial.printf("Progress: %d%%\n", updateProgress);
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

void WebApp::_api_get_ssid_list (AsyncWebServerRequest *request){
    request->send(200, "application/json", _scanAndSort());
}

// Accessors
bool WebApp::isConnectedToInternet () {
  return _connectedToInternet;
}





/* Scan available networks and sort them in order to their signal strength. */
String WebApp::_scanAndSort() {
   String json = "[";
   int n = WiFi.scanComplete();
   if(n == -2){
      WiFi.scanNetworks(true);
    } else if(n){
      int indices[n];
      for (int i = 0; i < n; i++) 
          indices[i] = i;
      for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
          if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
            std::swap(indices[i], indices[j]);
          }
        }
      }    
      
      for (int c = 0; c < n; c++){
        int i=indices[c];
        if(c) json += ",";
        json += "{";
        json += "\"rssi\":"+String(WiFi.RSSI(i));
        json += ",\"ssid\":\""+WiFi.SSID(i)+"\"";
        json += ",\"bssid\":\""+WiFi.BSSIDstr(i)+"\"";
        json += ",\"channel\":"+String(WiFi.channel(i));
        json += ",\"secure\":"+String(WiFi.encryptionType(i));
        json += "}";
      }
      WiFi.scanDelete();
      if(WiFi.scanComplete() == -2){
        WiFi.scanNetworks(true);
      }
    }
    json += "]";
    DEBUG ("Wifi list done");
    return (json); 
}


const char* WebApp::WlStatusToStr(wl_status_t wlStatus)
{
  switch (wlStatus)
  {
  case WL_NO_SHIELD: return "WL_NO_SHIELD";
  case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
  case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
  case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
  case WL_CONNECTED: return "WL_CONNECTED";
  case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
  case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
  case WL_DISCONNECTED: return "WL_DISCONNECTED";
  default: return "Unknown";
  }
}
