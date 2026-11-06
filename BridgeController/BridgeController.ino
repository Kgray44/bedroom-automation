/*
  Doorlight: standalone ESP32 Wi-Fi bridge for an existing fingerprint door lock
  and the Bedroom_Lights ESP32-S3 high-density strip controller.

  REQUIRED: WebSockets by Markus Sattler; ArduinoJson (v6/v7).
  INCLUDED WITH ESP32 CORE: WiFi, WebServer, HTTPClient, ESPmDNS, Preferences,
                            ArduinoOTA, Update.

  Connections: ESP32 GPIO23 -> resistor -> small LED -> GND. Use an appropriately
  driven MOSFET/transistor, separate supply, and common ground for a real lamp.
  NO SENSOR is wired to this ESP32. Original lock's WebSocket is the sensor feed.

  Security: local trusted Wi-Fi only. HTTP on the original lock uses the fixed
  ESPSESSIONID=1 cookie verified in its firmware. The original WebSocket does
  not authenticate clients; isolate both devices on a trusted network.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <esp_arduino_version.h>
#include <time.h>

#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#include "Secrets.example.h"
#endif
#include "Dashboard.h"
#include "NightLogic.h"

// ESP32-S3 GPIO23 is unavailable on many board profiles. Use GPIO18 there;
// classic ESP32 boards keep the original GPIO23 default.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
static constexpr int OUTPUT_PIN = 18;
#else
static constexpr int OUTPUT_PIN = 23;
#endif
static const char* HOSTNAME = "doorlight";
static const char* EASTERN_TZ = "EST5EDT,M3.2.0/2,M11.1.0/2";
static const char* EVENT_NAMES[] = {"motion", "walkby", "loiter"};
static const uint8_t EVENT_COUNT = 3;
static const size_t MAX_BODY_BYTES = 3000;

struct EventRule {
  bool enabled, pinEnabled, roomEnabled;
  uint16_t periodMs, durationMs;
  uint32_t cooldownMs;
  String color;
  uint8_t brightness; // Bedroom strip brightness, range 1..64
  uint8_t pinBrightness; // Local PWM indicator brightness, range 0..255
};
struct BridgeConfig {
  String lockHost = "fingerprintlock.local";
  String bedroomHost = "bedroom-leds.local";
  bool bedroomEnabled = false;
  bool scheduleEnabled = true;
  String everyoneTime = "06:15";
  String roommatesTime = "08:00";
  // Night Mode affects only temporary Doorlight indicators, not ambient scenes.
  bool nightEnabled = true;
  String nightStart = "18:00";
  String nightEnd = "07:00";
  uint8_t nightDimPercent = 50;
  EventRule events[3] = {
    {true,true,false,1200,2400,8000,"FFB85B",40,255},
    {true,true,false, 600,3000,25000,"45CFFF",40,255},
    {true,true,false,2200,6600,45000,"B78FFF",34,255}
  };
} cfg;

struct FlashState { bool active=false; uint8_t kind=0; uint32_t started=0; } flash;
// Bedroom effects are managed by BedroomSnapshotEngine.h (no strip firmware patch).
WebServer server(80);
WebSocketsClient lockWs;
Preferences prefs;
bool lockConnected=false;
bool lastNightModeActive=false;
bool nightStateInitialized=false;
bool otaUploadAccepted=false;
bool rebootRequested=false;
uint32_t rebootAt=0;
uint32_t lastSchedulerCheck=0;
uint32_t lastScheduleAttempt=0;
uint32_t lastEventAt[3] = {0,0,0};
bool hasSeenEvent[3] = {false,false,false};
uint32_t lastAppliedSlot=0;
String lockMode="unknown";
String lastModeResult="none";
uint32_t lockSnapshotSeq=0;
uint32_t failedScheduleSlot=0,failedScheduleAt=0;
struct ModeCommand {
  bool active=false, awaiting=false, wantedEveryone=false;
  uint8_t attempts=0;
  uint32_t sentAt=0, nextAttemptAt=0, expectedSeq=0, scheduleSlot=0;
  String source="";
} modeCommand;

// Strip engine state is surfaced by GET /api/bedroom/status.
String bedroomLastResult="not contacted";
String recentLogs[12];

bool timeAvailable() { return time(nullptr) > 1730000000; }
String localClock() {
  if (!timeAvailable()) return "not synchronized";
  time_t n=time(nullptr); struct tm t; localtime_r(&n,&t);
  char b[30]; strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S",&t);
  return String(b);
}
void logLine(const String& m) {
  for (int i=11;i>0;--i) recentLogs[i]=recentLogs[i-1];
  recentLogs[0]=localClock()+"  "+m;
  Serial.println(recentLogs[0]);
}
void jsonReply(int status, JsonDocument& d) {
  String out; serializeJson(d,out);
  server.send(status,"application/json",out);
}
void reply(int status,const String& message) {
  StaticJsonDocument<256> d; d["ok"]=status<400;
  if (status<400) d["message"]=message; else d["error"]=message;
  jsonReply(status,d);
}

bool validSecrets() {
  return strlen(BRIDGE_WIFI_SSID)>0 &&
    String(BRIDGE_WIFI_SSID)!="YOUR_WIFI_SSID" &&
    strlen(BRIDGE_ADMIN_PASSWORD)>=16 &&
    !String(BRIDGE_ADMIN_PASSWORD).startsWith("REPLACE_") &&
    strlen(BRIDGE_API_KEY)>=16 &&
    !String(BRIDGE_API_KEY).startsWith("REPLACE_") &&
    strlen(BRIDGE_OTA_PASSWORD)>=16 &&
    !String(BRIDGE_OTA_PASSWORD).startsWith("REPLACE_");
}
bool adminAuthenticated() { return server.authenticate(BRIDGE_ADMIN_USER,BRIDGE_ADMIN_PASSWORD); }
bool adminRequired() {
  if (adminAuthenticated()) return true;
  server.requestAuthentication(); return false;
}
bool authorized() {
  if (adminAuthenticated()) return true;
  if (server.hasHeader("X-API-Key") && server.header("X-API-Key")==BRIDGE_API_KEY) return true;
  server.requestAuthentication(); return false;
}
bool parseBody(StaticJsonDocument<3072>& d) {
  if (!server.hasArg("plain") || server.arg("plain").length()>MAX_BODY_BYTES) { reply(400,"Missing or oversized JSON body"); return false; }
  if (deserializeJson(d,server.arg("plain"))) { reply(400,"Malformed JSON"); return false; }
  if (!d.is<JsonObject>()) { reply(400,"Expected JSON object"); return false; }
  return true;
}
bool validHost(const String& h) {
  if (h.length()<3||h.length()>80||h[0]=='.'||h[0]=='-'||h.endsWith(".")||h.endsWith("-"))return false;
  for (size_t i=0;i<h.length();i++) {
    char c=h[i]; if (!(isalnum((unsigned char)c)||c=='.'||c=='-'))return false;
  }
  return true;
}
bool validTime(const String& x) {
  if (x.length()!=5||x[2]!=':')return false;
  for (int i : {0,1,3,4}) if (x[i]<'0'||x[i]>'9')return false;
  int h=x.substring(0,2).toInt(),m=x.substring(3).toInt();
  return h<24&&m<60;
}
// Local Eastern minutes are used for night mode; no extra sensor or location calls.
// Handles both same-day windows and overnight intervals across midnight.
bool nightModeActive() {
  if(!cfg.nightEnabled || !timeAvailable())return false;
  time_t now=time(nullptr);struct tm t;localtime_r(&now,&t);
  int current=t.tm_hour*60+t.tm_min;
  int start=cfg.nightStart.substring(0,2).toInt()*60+cfg.nightStart.substring(3).toInt();
  int end=cfg.nightEnd.substring(0,2).toInt()*60+cfg.nightEnd.substring(3).toInt();
  return inTimeWindowMinutes(current,start,end);
}
uint8_t nightAdjustedBrightness(uint8_t brightness) {
  uint8_t reduction=nightModeActive()?cfg.nightDimPercent:0;
  return applyBrightnessReduction(brightness,reduction);
}
// LEDC abstraction supports Arduino-ESP32 cores v2 and v3.
static constexpr uint8_t PWM_CHANNEL=0;
void initIndicator() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if(!ledcAttach(OUTPUT_PIN,5000,8)) Serial.println("Warning: PWM output initialization failed");
#else
  ledcSetup(PWM_CHANNEL,5000,8);
  ledcAttachPin(OUTPUT_PIN,PWM_CHANNEL);
#endif
}
void writeIndicator(uint8_t value) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(OUTPUT_PIN,value);
#else
  ledcWrite(PWM_CHANNEL,value);
#endif
}
bool validColor(const String& color) {
  if (color.length()!=6)return false;
  for (size_t i=0;i<color.length();i++) if (!isxdigit((unsigned char)color[i]))return false;
  return true;
}
int eventIndex(const String& name) {
  for (int i=0;i<3;i++)if(name==EVENT_NAMES[i])return i;
  return -1;
}
void writeEvent(JsonObject o,const EventRule& r) {
  o["enabled"]=r.enabled; o["pinEnabled"]=r.pinEnabled; o["roomEnabled"]=r.roomEnabled;
  o["periodMs"]=r.periodMs; o["durationMs"]=r.durationMs;
  o["cooldownMs"]=r.cooldownMs; o["color"]=r.color; o["brightness"]=r.brightness;
  o["pinBrightness"]=r.pinBrightness;
}
void buildConfig(JsonDocument& d) {
  d["ok"]=true;
  JsonObject c=d.createNestedObject("connections");
  c["lockHost"]=cfg.lockHost;c["bedroomHost"]=cfg.bedroomHost;c["bedroomEnabled"]=cfg.bedroomEnabled;
  JsonObject s=d.createNestedObject("schedule");
  s["enabled"]=cfg.scheduleEnabled;s["everyone"]=cfg.everyoneTime;s["roommates"]=cfg.roommatesTime;
  JsonObject night=d.createNestedObject("nightMode");
  night["enabled"]=cfg.nightEnabled;night["start"]=cfg.nightStart;night["end"]=cfg.nightEnd;
  night["dimPercent"]=cfg.nightDimPercent;
  JsonObject events=d.createNestedObject("events");
  for(int i=0;i<3;i++)writeEvent(events.createNestedObject(EVENT_NAMES[i]),cfg.events[i]);
}
void saveSettings() {
  StaticJsonDocument<3072> d;buildConfig(d);String text;serializeJson(d,text);
  prefs.putString("config",text);
}
void loadSettings() {
  String raw=prefs.getString("config","");
  if(raw.length()==0)return;
  StaticJsonDocument<3072>d;if(deserializeJson(d,raw))return;
  JsonObjectConst c=d["connections"];
  String lh=c["lockHost"] | cfg.lockHost;
  String bh=c["bedroomHost"] | cfg.bedroomHost;
  if(validHost(lh))cfg.lockHost=lh;if(validHost(bh))cfg.bedroomHost=bh;
  if(c["bedroomEnabled"].is<bool>())cfg.bedroomEnabled=c["bedroomEnabled"].as<bool>();
  JsonObjectConst s=d["schedule"];
  String e=s["everyone"] | cfg.everyoneTime;
  String r=s["roommates"] | cfg.roommatesTime;
  if(validTime(e)&&validTime(r)&&e!=r){cfg.everyoneTime=e;cfg.roommatesTime=r;}
  if(s["enabled"].is<bool>())cfg.scheduleEnabled=s["enabled"].as<bool>();
  JsonObjectConst night=d["nightMode"];
  String ns=night["start"] | cfg.nightStart;
  String ne=night["end"] | cfg.nightEnd;
  if(validTime(ns)&&validTime(ne)&&ns!=ne){cfg.nightStart=ns;cfg.nightEnd=ne;}
  if(night["enabled"].is<bool>())cfg.nightEnabled=night["enabled"].as<bool>();
  int nd=night["dimPercent"] | (int)cfg.nightDimPercent;
  if(nd>=0&&nd<=100)cfg.nightDimPercent=nd;
  JsonObjectConst ev=d["events"];
  for(int i=0;i<3;i++) {
    JsonObjectConst x=ev[EVENT_NAMES[i]];
    if(x.isNull())continue;
    EventRule& rule=cfg.events[i];
    if(x["enabled"].is<bool>())rule.enabled=x["enabled"].as<bool>();
    if(x["pinEnabled"].is<bool>())rule.pinEnabled=x["pinEnabled"].as<bool>();
    if(x["roomEnabled"].is<bool>())rule.roomEnabled=x["roomEnabled"].as<bool>();
    int period=x["periodMs"] | (int)rule.periodMs;
    int duration=x["durationMs"] | (int)rule.durationMs;
    int cooldown=x["cooldownMs"] | (int)rule.cooldownMs;
    int brightness=x["brightness"] | (int)rule.brightness;
    int pinBrightness=x["pinBrightness"] | (int)rule.pinBrightness;
    String color=x["color"] | rule.color;
    if(period>=350&&period<=4000)rule.periodMs=period;
    if(duration>=300&&duration<=10000)rule.durationMs=duration;
    if(cooldown>=0&&cooldown<=600000)rule.cooldownMs=cooldown;
    if(brightness>=1&&brightness<=64)rule.brightness=brightness;
    if(pinBrightness>=0&&pinBrightness<=255)rule.pinBrightness=pinBrightness;
    if(validColor(color)){color.toUpperCase();rule.color=color;}
  }
}

void triggerEvent(uint8_t i,bool isTest);
void dlAutomationTrigger(const String& trigger);
void dlAutomationTick();
void dlSetupRoutes();
void dlLoadConfig();
void dlLoadRules();
void dlIndicatorTick();
void dlAutomationOnModeSnapshot();
bool r4GamingSuppresses(const String&);
void r4OnLockLog(const String&);
void r4OnOutsideMotion();
void r4OnEntry();
void r4SetupRoutes();
void r4Load();
void r4Tick();
bool dlExpectedReadback(uint32_t,uint32_t,bool,bool,bool);


void onLockMessage(WStype_t type,uint8_t* payload,size_t length) {
  if(type==WStype_CONNECTED){lockConnected=true;logLine("Door WebSocket connected; awaiting automatic status push");return;}
  if(type==WStype_DISCONNECTED){lockConnected=false;lockMode="offline";logLine("Door WebSocket disconnected");return;}
  if(type!=WStype_TEXT||length>1024)return;
  StaticJsonDocument<1200>d;
  if(deserializeJson(d,payload,length))return;
  if(d["everyone"].is<bool>() && d["roommates"].is<bool>()) {
    lockMode=d["everyone"].as<bool>()?"everyone":d["roommates"].as<bool>()?"roommates":"justMe";
    lockSnapshotSeq++;
    dlAutomationOnModeSnapshot();
  }
  String s=d["log"] | "";
  r4OnLockLog(s);
  if(s.indexOf("Someone is LOITERING outside")>=0) {
    // Higher priority than transient motion.
    triggerEvent(2,false);dlAutomationTrigger("loiter");
  } else if(s.indexOf("Walk-by detected")>=0) {
    triggerEvent(1,false);dlAutomationTrigger("walkby");
  } else if(s.indexOf("Fresh outside motion detected")>=0) {
    triggerEvent(0,false);r4OnOutsideMotion();dlAutomationTrigger("motion");
  } else if(s.indexOf("Person ENTERED")>=0) {
    r4OnEntry();dlAutomationTrigger("entry");
  } else if(s.indexOf("Person EXITED")>=0) {
    dlAutomationTrigger("exit");
  } else if(s.indexOf("Door left open!")>=0) {
    dlAutomationTrigger("door_left_open");
  }
}

void configureLockSocket() {
  lockWs.disconnect();lockConnected=false;
  lockWs.begin(cfg.lockHost.c_str(),80,"/ws");
  lockWs.onEvent(onLockMessage);
  lockWs.setReconnectInterval(4000);
  lockWs.enableHeartbeat(15000,3000,2);
}

bool bedroomQueueEvent(uint8_t i);
void bedroomCancel();
bool bedroomSessionBusy();
void triggerEvent(uint8_t i,bool isTest) {
  if(i>=3)return;
  if(!isTest && r4GamingSuppresses(EVENT_NAMES[i]))return;
  EventRule& r=cfg.events[i];
  if(!r.enabled&&!isTest)return;
  uint32_t now=millis();
  if(!isTest && hasSeenEvent[i] && (uint32_t)(now-lastEventAt[i])<r.cooldownMs)return;
  if(!isTest){hasSeenEvent[i]=true;lastEventAt[i]=now;}
  if(flash.active) {
    const EventRule& current=cfg.events[flash.kind];
    if((uint32_t)(now-flash.started)<current.durationMs && i<flash.kind)return;
  }
  flash.active=true;flash.kind=i;flash.started=now;
  writeIndicator(0); // PWM ramp is handled by serviceLight(), not an instant jump.
  if(cfg.bedroomEnabled && r.roomEnabled && !bedroomQueueEvent(i))
    logLine("Bedroom effect not queued (see /api/bedroom/status)");
  logLine(String(isTest?"Test":"Detected")+": "+EVENT_NAMES[i]);
}

void serviceLight() {
  uint32_t now=millis();
  if(!flash.active){writeIndicator(0);return;}
  EventRule& r=cfg.events[flash.kind];
  uint32_t elapsed=(uint32_t)(now-flash.started);
  if(elapsed>=r.durationMs){flash.active=false;writeIndicator(0);return;}
  uint16_t period=r.periodMs;
  uint32_t phase=elapsed%period;
  uint32_t half=period/2;
  if(!r.pinEnabled||phase>=half){writeIndicator(0);return;}
  // Smoothstep edges replace hard digital toggles; keep transitions shorter
  // than the half-cycle so the pulse still reaches its requested peak.
  uint32_t attack=min((uint32_t)180,max((uint32_t)30,half/3));
  uint32_t release=min((uint32_t)240,max((uint32_t)30,half/3));
  auto ease=[](float x)->float{x=constrain(x,0.0f,1.0f);return x*x*(3.0f-2.0f*x);};
  float envelope=ease((float)phase/(float)attack)*
    ease((float)(half-phase)/(float)release);
  uint32_t endRemaining=r.durationMs-elapsed;
  envelope*=ease((float)endRemaining/180.0f);
  writeIndicator((uint8_t)lroundf(nightAdjustedBrightness(r.pinBrightness)*envelope));
}

// HTTP 200 means accepted, not verified. Only WebSocket status readback
// confirms the desired mode. Retry up to 3 times with bounded delays.
bool sendLockMode(bool everyoneMode) {
  if(WiFi.status()!=WL_CONNECTED)return false;
  WiFiClient client;HTTPClient http;
  String url="http://"+cfg.lockHost+(everyoneMode?"/everyone?state=1":"/roommates?state=1");
  http.setConnectTimeout(650);http.setTimeout(1200);
  if(!http.begin(client,url))return false;
  http.addHeader("Cookie","ESPSESSIONID=1");
  int code=http.GET();String body=http.getString();http.end();
  return code==200 && body.indexOf("OK")>=0;
}
bool queueModeCommand(bool everyoneMode,const String& source,uint32_t scheduleSlot=0) {
  if(modeCommand.active) {
    if(modeCommand.wantedEveryone==everyoneMode && modeCommand.scheduleSlot==scheduleSlot)return true;
    if(source!="manual")return false;
    logLine("Manual mode overrides pending scheduled command");
  }
  modeCommand=ModeCommand();modeCommand.active=true;
  modeCommand.wantedEveryone=everyoneMode;modeCommand.source=source;
  modeCommand.scheduleSlot=scheduleSlot;
  modeCommand.nextAttemptAt=millis();
  lastModeResult=String(everyoneMode?"Everyone":"Roommates")+" queued ("+source+")";
  logLine(lastModeResult);return true;
}
void dlAutomationOnModeSnapshot(){
  if(!modeCommand.active||!modeCommand.awaiting)return;
  if(lockSnapshotSeq<=modeCommand.expectedSeq)return;
  const String wanted=modeCommand.wantedEveryone?"everyone":"roommates";
  if(!dlExpectedReadback(lockSnapshotSeq,modeCommand.expectedSeq,
      modeCommand.wantedEveryone,lockMode=="everyone",lockMode=="roommates"))return;
  lastModeResult="CONFIRMED "+wanted+" via lock read-back (attempt "+String(modeCommand.attempts)+")";
  logLine(lastModeResult);
  if(modeCommand.scheduleSlot){
    lastAppliedSlot=modeCommand.scheduleSlot;
    prefs.putUInt("lastSlot",lastAppliedSlot);
  }
  modeCommand.active=false;
}
void serviceReliableMode(){
  if(!modeCommand.active)return;
  uint32_t now=millis();
  if(modeCommand.awaiting) {
    if((uint32_t)(now-modeCommand.sentAt)<2400)return;
    modeCommand.awaiting=false;
    logLine("Door read-back timed out or did not match");
    modeCommand.nextAttemptAt=now+1300;
  }
  if((int32_t)(now-modeCommand.nextAttemptAt)<0)return;
  if(modeCommand.attempts>=3){
    lastModeResult="FAILED after 3 attempts: "+String(modeCommand.wantedEveryone?"everyone":"roommates")+
      " (lock status not verified)";
    if(modeCommand.scheduleSlot){failedScheduleSlot=modeCommand.scheduleSlot;failedScheduleAt=now;}
    logLine(lastModeResult);modeCommand.active=false;return;
  }
  modeCommand.attempts++;
  modeCommand.expectedSeq=lockSnapshotSeq;
  bool accepted=sendLockMode(modeCommand.wantedEveryone);
  modeCommand.sentAt=millis();modeCommand.nextAttemptAt=modeCommand.sentAt+1300;
  modeCommand.awaiting=accepted && lockConnected;
  lastModeResult="Door mode "+String(modeCommand.wantedEveryone?"everyone":"roommates")+
    " attempt "+String(modeCommand.attempts)+"/3: "+
    (accepted?(lockConnected?"awaiting read-back":"WebSocket offline"):"HTTP failed");
  logLine(lastModeResult);
  if(modeCommand.awaiting) lockWs.sendTXT("getStatus");
}

uint32_t slotForDate(const tm& date,int mode) {
  return (uint32_t)((date.tm_year+1900)*400+date.tm_yday)*2+(mode==1?1:0);
}
struct ScheduleTarget { bool valid=false; uint32_t slot=0; bool everyone=false; };
// Most recent boundary (today or yesterday). mktime handles Eastern DST.
ScheduleTarget currentTarget(time_t epoch) {
  ScheduleTarget best;time_t bestEpoch=0;
  struct tm local;localtime_r(&epoch,&local);
  String times[2]={cfg.everyoneTime,cfg.roommatesTime};
  for(int offset=-1;offset<=0;offset++)for(int mode=0;mode<2;mode++) {
    struct tm boundary=local;
    boundary.tm_mday+=offset;
    boundary.tm_hour=times[mode].substring(0,2).toInt();
    boundary.tm_min=times[mode].substring(3).toInt();
    boundary.tm_sec=0;boundary.tm_isdst=-1;
    time_t t=mktime(&boundary);
    if(t>0&&t<=epoch&&(!best.valid||t>bestEpoch)) {
      best.valid=true;bestEpoch=t;
      best.everyone=(mode==0);best.slot=slotForDate(boundary,mode);
    }
  }
  return best;
}
void scheduleTick() {
  if(!cfg.scheduleEnabled || !timeAvailable() || WiFi.status()!=WL_CONNECTED)return;
  ScheduleTarget target=currentTarget(time(nullptr));
  if(!target.valid || target.slot==lastAppliedSlot)return;
  if(modeCommand.active)return;
  if(target.slot==failedScheduleSlot && (uint32_t)(millis()-failedScheduleAt)<300000UL)return;
  if((uint32_t)(millis()-lastScheduleAttempt)<10000)return;
  lastScheduleAttempt=millis();
  queueModeCommand(target.everyone,"schedule",target.slot);
}

void serviceNightMode() {
  bool active=nightModeActive();
  if(nightStateInitialized && active!=lastNightModeActive) {
    logLine(String("Night Mode ")+(active?"started":"ended"));
    // New alerts use the updated night brightness. Active effects finish unchanged.
    dlAutomationTrigger(active?"night_start":"night_end");
  }
  lastNightModeActive=active;nightStateInitialized=true;
}
void handleStatus() {
  if(!authorized())return;
  StaticJsonDocument<2700>d;
  d["ok"]=true;d["wifiConnected"]=WiFi.status()==WL_CONNECTED;
  d["lockConnected"]=lockConnected;d["lockMode"]=lockMode;
  d["bedroomLastResult"]=bedroomLastResult;d["lastModeResult"]=lastModeResult;
  d["modeCommandPending"]=modeCommand.active;
  d["modeCommandAttempts"]=modeCommand.attempts;
  d["modeCommandAwaitingReadback"]=modeCommand.awaiting;
  d["timeValid"]=timeAvailable();d["localTime"]=localClock();
  d["nightModeEnabled"]=cfg.nightEnabled;d["nightModeActive"]=nightModeActive();
  d["nightModeDimPercent"]=cfg.nightDimPercent;
  d["ip"]=WiFi.localIP().toString();d["uptimeSeconds"]=millis()/1000UL;
  d["alertActive"]=flash.active;d["currentEvent"]=flash.active?EVENT_NAMES[flash.kind]:"none";
  JsonArray logs=d.createNestedArray("logs");
  for(const auto& line:recentLogs)if(line.length())logs.add(line);
  jsonReply(200,d);
}
void handleConfig() { if(!authorized())return;StaticJsonDocument<3072>d;buildConfig(d);jsonReply(200,d); }
void handleNightModeGet() {
  if(!authorized())return;
  StaticJsonDocument<384>d;d["ok"]=true;d["enabled"]=cfg.nightEnabled;
  d["start"]=cfg.nightStart;d["end"]=cfg.nightEnd;
  d["dimPercent"]=cfg.nightDimPercent;d["active"]=nightModeActive();
  d["timeValid"]=timeAvailable();jsonReply(200,d);
}
void handleNightModePut() {
  if(!authorized())return;
  StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String ns=d["start"] | cfg.nightStart;
  String ne=d["end"] | cfg.nightEnd;
  int dim=d["dimPercent"] | (int)cfg.nightDimPercent;
  if(!validTime(ns)||!validTime(ne)||ns==ne) {reply(400,"Use distinct HH:MM times for night start/end");return;}
  if(dim<0||dim>100){reply(400,"dimPercent must be 0..100");return;}
  bool enabled=cfg.nightEnabled;
  if(!d["enabled"].isNull()){
    if(!d["enabled"].is<bool>()){reply(400,"enabled must be boolean");return;}
    enabled=d["enabled"].as<bool>();
  }
  bool changed=(enabled!=cfg.nightEnabled||ns!=cfg.nightStart||ne!=cfg.nightEnd||dim!=cfg.nightDimPercent);
  cfg.nightEnabled=enabled;cfg.nightStart=ns;cfg.nightEnd=ne;cfg.nightDimPercent=dim;
  if(changed){saveSettings();logLine("Night Mode settings saved");}
  lastNightModeActive=nightModeActive();nightStateInitialized=true;
  handleNightModeGet();
}
void handleSchedule() {
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String e=d["everyone"] | cfg.everyoneTime,r=d["roommates"] | cfg.roommatesTime;
  if(!validTime(e)||!validTime(r)||e==r){reply(400,"Set two distinct HH:MM times");return;}
  cfg.everyoneTime=e;cfg.roommatesTime=r;
  if(d["enabled"].is<bool>())cfg.scheduleEnabled=d["enabled"].as<bool>();
  // Reconcile once against the new schedule, instead of waiting an entire day.
  lastAppliedSlot=0;prefs.putUInt("lastSlot",0);lastScheduleAttempt=millis()-11000UL;
  failedScheduleSlot=0;modeCommand.active=false;
  saveSettings();logLine("Schedule updated");reply(200,"Schedule updated; next check applies current window");
}
void handleConnections() {
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String lh=d["lockHost"] | cfg.lockHost,bh=d["bedroomHost"] | cfg.bedroomHost;
  if(!validHost(lh)||!validHost(bh)){reply(400,"Hosts must be local hostnames or IPv4, without port or scheme");return;}
  if(bh!=cfg.bedroomHost && bedroomSessionBusy()){reply(409,"Finish or resolve bedroom restoration before changing host");return;}
  bool changed=lh!=cfg.lockHost;
  cfg.lockHost=lh;cfg.bedroomHost=bh;
  if(d["bedroomEnabled"].is<bool>())cfg.bedroomEnabled=d["bedroomEnabled"].as<bool>();
  saveSettings();if(changed)configureLockSocket();reply(200,"Connections saved");
}
void handleEventConfig() {
  if(!authorized())return;
  int i=eventIndex(server.uri().substring(String("/api/events/").length()));
  if(i<0){reply(404,"Unknown event");return;}
  StaticJsonDocument<3072>d;if(!parseBody(d))return;
  EventRule next=cfg.events[i];
  if(d["enabled"].is<bool>())next.enabled=d["enabled"].as<bool>();
  if(d["pinEnabled"].is<bool>())next.pinEnabled=d["pinEnabled"].as<bool>();
  if(d["roomEnabled"].is<bool>())next.roomEnabled=d["roomEnabled"].as<bool>();
  int period=d["periodMs"] | (int)next.periodMs;
  int duration=d["durationMs"] | (int)next.durationMs;
  int cooldown=d["cooldownMs"] | (int)next.cooldownMs;
  int brightness=d["brightness"] | (int)next.brightness;
  int pinBrightness=d["pinBrightness"] | (int)next.pinBrightness;
  String color=d["color"] | next.color;
  if(period<350||period>4000||duration<300||duration>10000||cooldown<0||cooldown>600000||brightness<1||brightness>64||pinBrightness<0||pinBrightness>255||!validColor(color)){
    reply(400,"Invalid pattern: period 350..4000 ms, duration 300..10000 ms, cooldown 0..600000 ms, brightness 1..64, pinBrightness 0..255, color RRGGBB");return;
  }
  next.periodMs=period;next.durationMs=duration;next.cooldownMs=cooldown;next.brightness=brightness;next.pinBrightness=pinBrightness;
  color.toUpperCase();next.color=color;cfg.events[i]=next;saveSettings();
  reply(200,String(EVENT_NAMES[i])+" updated");
}
void handleTest() {
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String type=d["type"] | "";int i=eventIndex(type);
  if(i<0){reply(400,"type must be motion, walkby or loiter");return;}
  triggerEvent(i,true);reply(200,"Test alert started");
}
void handleManualMode() {
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String mode=d["mode"] | "";
  if(mode!="everyone"&&mode!="roommates"){reply(400,"mode must be everyone or roommates");return;}
  if(queueModeCommand(mode=="everyone","manual"))reply(202,"Command queued; watch /api/status for CONFIRMED or FAILED");
  else reply(409,"Another mode command is in progress");
}
void handleCancel() {
  if(!authorized())return;
  flash.active=false;writeIndicator(0);bedroomCancel();
  reply(202,"Local light stopped; bedroom restoration queued and will be verified");
}
void handleDocs() {
  if(!authorized())return;
  server.send(200,"text/plain; charset=utf-8",
    "Doorlight REST v1 (local HTTP; use Basic Auth or X-API-Key)\n"
    "GET /health                            public, no secrets\n"
    "GET /api/status                       live states, event log\n"
    "GET /api/config                       saved settings\n"
    "PUT /api/schedule  {\"everyone\":\"06:15\",\"roommates\":\"08:00\",\"enabled\":true}\n"
    "GET /api/night-mode  night enabled, active, schedule and dim percentage\n"
    "PUT /api/night-mode {\"enabled\":true,\"start\":\"18:00\",\"end\":\"07:00\",\"dimPercent\":50}\n"
    "PUT /api/connections {\"lockHost\":\"fingerprintlock.local\",\"bedroomHost\":\"bedroom-leds.local\",\"bedroomEnabled\":true}\n"
    "PUT /api/events/motion|walkby|loiter   event JSON fields from /api/config (including pinBrightness)\n"
    "POST /api/events/test {\"type\":\"motion\"|\"walkby\"|\"loiter\"}\n"
    "POST /api/mode {\"mode\":\"everyone\"|\"roommates\"}\n"
    "POST /api/alerts/cancel {}\n"
    "GET/PUT /api/bedroom/fades          native fade controls (0/500/1500/3000/5000ms)\n"
    "GET /api/bedroom/status            snapshot / restore status\n"
    "GET /api/bedroom/live              current strip settings, no writes\n"
    "POST /api/bedroom/test             temporary effect, default 5 sec\n"
    "POST /api/bedroom/cancel           request verified restoration\n"
    "POST /api/bedroom/recover          {\"force\":false} for safe recovery\n"
    "POST /api/bedroom/forget           {\"confirm\":\"FORGET\"} discard snapshot\n"
    "GET /api/bulbs                        live Home Assistant Matter bulb states\n"
    "PUT /api/bulbs/config                HA host, port, bulb entities, person entity\n"
    "POST /api/bulbs/control              bulb target, on/off/toggle, color, brightness, transition\n"
    "GET/PUT/DELETE /api/shortcuts        automation library and editing\n"
    "POST /api/shortcuts/run             run shortcut ID on demand\n"
    "POST /api/shortcuts/countdown       one-shot in-RAM timer {id,delaySeconds}\n"
    "GET /api/automation/status           shortcut runtime status\n"
    "GET /update  (admin Basic Auth only; browser OTA)\n"
    "Example: curl -X PUT -H 'X-API-Key: KEY' -H 'Content-Type: application/json' -d '{\"everyone\":\"06:15\",\"roommates\":\"08:00\"}' http://doorlight.local/api/schedule\n"
  );
}
#include "SmartAutomations.h"
#include "RoomEnhancements.h"

static const char OTA_PAGE[] PROGMEM=R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><style>body{font:16px system-ui;background:#0a1324;color:#dceaff;display:grid;place-content:center;min-height:80vh}form{background:#17273e;padding:30px;border-radius:18px;border:1px solid #49617c}button{padding:10px;background:#518fcc;border:0;border-radius:8px;color:white}</style><form method="POST" action="/update" enctype="multipart/form-data"><h2>Doorlight firmware update</h2><p>Select a compiled ESP32 .bin with OTA-compatible partition layout.</p><input type="file" name="firmware" accept=".bin" required><button>Install update</button><p><a href="/">Return to dashboard</a></p></form>)HTML";
void setupWeb() {
  const char* headers[]={"X-API-Key"};server.collectHeaders(headers,1);
  server.on("/",HTTP_GET,[](){if(adminRequired())server.send_P(200,"text/html",DASHBOARD_HTML);});
  server.on("/health",HTTP_GET,[](){reply(200,"Doorlight running");});
  server.on("/api/status",HTTP_GET,handleStatus);
  server.on("/api/config",HTTP_GET,handleConfig);
  server.on("/api/schedule",HTTP_PUT,handleSchedule);
  server.on("/api/night-mode",HTTP_GET,handleNightModeGet);
  server.on("/api/night-mode",HTTP_PUT,handleNightModePut);
  server.on("/api/connections",HTTP_PUT,handleConnections);
  server.on("/api/events/test",HTTP_POST,handleTest);
  server.on("/api/events/motion",HTTP_PUT,handleEventConfig);
  server.on("/api/events/walkby",HTTP_PUT,handleEventConfig);
  server.on("/api/events/loiter",HTTP_PUT,handleEventConfig);
  server.on("/api/mode",HTTP_POST,handleManualMode);
  server.on("/api/alerts/cancel",HTTP_POST,handleCancel);
  server.on("/api/docs",HTTP_GET,handleDocs);
  dlSetupRoutes();r4SetupRoutes();
  server.on("/update",HTTP_GET,[](){if(adminRequired())server.send_P(200,"text/html",OTA_PAGE);});
  server.on("/update",HTTP_POST,
    [](){
      if(!adminAuthenticated()){server.requestAuthentication();return;}
      bool ok=otaUploadAccepted && !Update.hasError();
      server.send(ok?200:500,"text/plain",ok?"Firmware installed. Restarting...":"Update failed");
      if(ok){rebootRequested=true;rebootAt=millis()+1500;}
    },
    [](){
      HTTPUpload& u=server.upload();
      if(!adminAuthenticated())return;
      if(u.status==UPLOAD_FILE_START){otaUploadAccepted=Update.begin(UPDATE_SIZE_UNKNOWN);}
      else if(u.status==UPLOAD_FILE_WRITE && otaUploadAccepted){if(Update.write(u.buf,u.currentSize)!=u.currentSize)otaUploadAccepted=false;}
      else if(u.status==UPLOAD_FILE_END && otaUploadAccepted){otaUploadAccepted=Update.end(true);}
      else if(u.status==UPLOAD_FILE_ABORTED){Update.abort();otaUploadAccepted=false;}
    }
  );
  server.onNotFound([](){reply(404,"No such endpoint");});
  server.begin();
}

void setup() {
  Serial.begin(115200);
  pinMode(OUTPUT_PIN,OUTPUT);initIndicator();writeIndicator(0);
  if(!validSecrets()){
    Serial.println("ERROR: configure Secrets.h (Wi-Fi, admin password, API key, OTA password). Controller disabled.");
    while(true)delay(1000);
  }
  prefs.begin("doorlight",false);
  loadSettings();lastAppliedSlot=prefs.getUInt("lastSlot",0);
  dlFsReady=LittleFS.begin(false);
  // Format only on first-ever initialization, never after a successful mount.
  // A later mount failure is visible in /api/automation/status, not erased.
  if(!dlFsReady && !prefs.getBool("fsInitialized",false)){
    if(LittleFS.format())dlFsReady=LittleFS.begin(false);
  }
  if(dlFsReady)prefs.putBool("fsInitialized",true);
  dlLoadConfig();dlLoadRules();r4Load();bedroomLoadFades();bedroomLoadRecovery();
  WiFi.mode(WIFI_STA);WiFi.setHostname(HOSTNAME);WiFi.setAutoReconnect(true);
  WiFi.begin(BRIDGE_WIFI_SSID,BRIDGE_WIFI_PASSWORD);
  configTzTime(EASTERN_TZ,"pool.ntp.org","time.nist.gov");
  // Give Wi-Fi a chance before binding the HTTP listener and mDNS service.
  uint32_t connectStart=millis();
  while(WiFi.status()!=WL_CONNECTED && (uint32_t)(millis()-connectStart)<12000)delay(100);
  setupWeb();configureLockSocket();
  ArduinoOTA.setHostname(HOSTNAME);ArduinoOTA.setPassword(BRIDGE_OTA_PASSWORD);
  ArduinoOTA.onStart([](){flash.active=false;writeIndicator(0);});
  ArduinoOTA.begin();
  if(WiFi.status()==WL_CONNECTED && MDNS.begin(HOSTNAME))MDNS.addService("http","tcp",80);
  logLine("Bridge initialized. Waiting for network and time.");
}
void loop() {
  ArduinoOTA.handle();server.handleClient();lockWs.loop();serviceLight();dlAutomationTick();r4Tick();bedroomTick();bedroomLastResult=bedroom.result;serviceReliableMode();
  uint32_t now=millis();
  if((uint32_t)(now-lastSchedulerCheck)>=1000){lastSchedulerCheck=now;scheduleTick();serviceNightMode();}
  // The new ESP32 uses only the original bedroom firmware /api/state + /set routes.
  if(rebootRequested && (int32_t)(now-rebootAt)>=0)ESP.restart();
  delay(2);
}
