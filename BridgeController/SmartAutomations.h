#pragma once
// Doorlight v3: Home Assistant Matter bridge and local Shortcuts engine.
// This header is included after the original BridgeController handlers and
// before setupWeb(). No second sensor is used.
#include <LittleFS.h>
#include "ShortcutLogic.h"

static constexpr uint8_t DL_BULBS = 2;
static constexpr uint8_t DL_MAX_RULES = 20;
static constexpr uint8_t DL_MAX_ACTIONS = 3;
static const char* DL_FILE = "/doorlight-shortcuts.json";
static const char* DL_TRIGGERS[] = {"motion","walkby","loiter","entry","exit","door_left_open","night_start","night_end","time","interval","manual","gaming_start","gaming_end","pomodoro_work","pomodoro_break","pomodoro_done"};
static const char* DL_OPERATIONS[] = {"on","off","toggle","fade","pulse","sunrise","weatherSunrise"};
static const char* DL_TARGETS[] = {"both","bulb1","bulb2","bedroom","indicator"};
static const char* DL_CONDITIONS[] = {"any","meHome","meAway","night","day","roommates","everyone","fingerprintMe","fingerprintOther"};

template <size_t N> bool dlInList(const String& value,const char* const (&list)[N]) {
  for (const char* s:list) if (value==s) return true;
  return false;
}
bool dlValidEntity(const String& value,const char* domain) {
  String prefix=String(domain)+".";
  if(!value.startsWith(prefix)||value.length()>80||value.length()<=prefix.length())return false;
  for(size_t i=prefix.length();i<value.length();++i){char c=value[i];if(!(islower((unsigned char)c)||isdigit((unsigned char)c)||c=='_'))return false;}
  return true;
}
struct DlBulbConfig {
  bool enabled=false;
  String host="homeassistant.local";
  uint16_t port=8123;
  String entities[2]={"light.bedroom_1","light.bedroom_2"};
  String labels[2]={"Bedside","Ceiling"};
  String meEntity="";  // Home Assistant person.* or device_tracker.* (zone, not identity)
};
DlBulbConfig dlBulbConfig;
struct DlBulbState {
  bool valid=false, on=false;
  int brightness=0, kelvin=0;
  String color="FFD8A0", colorMode="", error="not connected";
  String contextId="";
  uint32_t checkedAt=0;
};
DlBulbState dlBulbs[2];
struct DlAction {
  String target="both",operation="on",color="FFCA8D";
  int brightness=60, kelvin=0, durationSec=0, transitionSec=1;
  bool restore=false;
  String stripMode="";   // Optional existing bedroom firmware animation mode
  int stripHalfMs=600;  // Strobe half-period, 100..5000 ms
};
struct DlRule {
  String id,name,trigger="manual",time="07:00",condition="any";
  bool enabled=true;
  uint8_t days=127;          // Mon=1 ... Sun=64
  uint16_t everyMinutes=0, cooldownSec=15;
  uint8_t actionCount=1;
  DlAction actions[DL_MAX_ACTIONS];
  uint32_t lastEventMs=0, lastEpochRun=0;
};
DlRule dlRules[DL_MAX_RULES];
uint8_t dlRuleCount=0;
bool dlFsReady=false;
String dlLastAction="Not configured",dlLastError="";
uint32_t dlLastBulbPoll=0,dlLastRuleTick=0;
struct DlTemporary {
  bool active=false, expectedOn=false;
  DlBulbState before;
  int expectedBrightness=0;
  String expectedColor="", expectedContextId="";
  bool sceneSaved=false;
  uint32_t untilMs=0,lastPulseMs=0;
  bool pulse=false, pulseIsOn=true;
  int pulseBrightness=20;
  String pulseColor="FFCA8D";
};
DlTemporary dlTemporary[2];
struct DlIndicator {
  bool active=false;
  uint32_t startMs=0, durationMs=0;
  int brightness=180, periodMs=1400;
} dlIndicator;
struct DlCountdown {bool active=false;String id;uint32_t due=0;} dlCountdown;

// Home Assistant token never appears in the dashboard, API, or NVS config.
// The user must supply BRIDGE_HA_TOKEN in Secrets.h to enable Matter control.
bool dlHAReady() {
  return dlBulbConfig.enabled && WiFi.status()==WL_CONNECTED &&
    strlen(BRIDGE_HA_TOKEN)>30 && !String(BRIDGE_HA_TOKEN).startsWith("REPLACE_");
}
bool dlHARequest(const String& path,const String& method,const String& body,String& response,int& code) {
  code=-1;response="";
  if(!dlHAReady()){dlLastError="Home Assistant is not configured or offline";return false;}
  WiFiClient client;HTTPClient http;
  String url="http://"+dlBulbConfig.host+":"+String(dlBulbConfig.port)+path;
  http.setConnectTimeout(650);http.setTimeout(1800);http.setReuse(false);
  if(!http.begin(client,url))return false;
  http.addHeader("Authorization",String("Bearer ")+BRIDGE_HA_TOKEN);
  http.addHeader("Content-Type","application/json");
  code=(method=="GET")?http.GET():http.POST(body);
  if(code>0 && code<400)response=http.getString();
  http.end();
  if(code<200||code>=300){dlLastError="Home Assistant HTTP "+String(code)+" on "+path;return false;}
  return true;
}
bool dlReadEntity(const String& entity,JsonDocument& doc) {
  String body;int code;
  if(!dlHARequest("/api/states/"+entity,"GET","",body,code))return false;
  return !deserializeJson(doc,body) && doc["state"].is<const char*>();
}
bool dlReadBulb(uint8_t i,DlBulbState& out) {
  if(i>=DL_BULBS)return false;
  DynamicJsonDocument doc(4096);
  if(!dlReadEntity(dlBulbConfig.entities[i],doc)){
    out.valid=false;out.error=dlHAReady()?"unavailable":"Home Assistant not configured";out.checkedAt=millis();return false;
  }
  String state=doc["state"]|"unavailable";
  if(state!="on"&&state!="off"){
    out.valid=false;out.error=state;out.checkedAt=millis();return false;
  }
  out.valid=true;out.on=(state=="on");
  out.brightness=constrain((int)(doc["attributes"]["brightness"]|0),0,255);
  out.kelvin=doc["attributes"]["color_temp_kelvin"]|0;
  out.colorMode=String(doc["attributes"]["color_mode"]|"");
  out.contextId=String(doc["context"]["id"]|"");
  JsonArrayConst rgb=doc["attributes"]["rgb_color"].as<JsonArrayConst>();
  if(rgb.size()==3){char b[7];snprintf(b,sizeof(b),"%02X%02X%02X",constrain((int)rgb[0],0,255),constrain((int)rgb[1],0,255),constrain((int)rgb[2],0,255));out.color=String(b);}
  out.checkedAt=millis();out.error="";return true;
}
bool dlSendService(uint8_t i,const String& operation,int pct,const String& color,int kelvin,int transition=0) {
  if(i>=DL_BULBS)return false;
  DynamicJsonDocument doc(360);
  doc["entity_id"]=dlBulbConfig.entities[i];
  if(operation!="off") {
    if(pct>=0)doc["brightness_pct"]=constrain(pct,1,100);
    if(kelvin>0)doc["color_temp_kelvin"]=kelvin;
    else if(color.length()==6 && validColor(color)){
      char c[3]={0,0,0};JsonArray a=doc.createNestedArray("rgb_color");
      for(int n=0;n<3;++n){c[0]=color[n*2];c[1]=color[n*2+1];a.add((int)strtol(c,nullptr,16));}
    }
  }
  if(transition>0)doc["transition"]=constrain(transition,0,900);
  String body;serializeJson(doc,body);String response;int code;
  bool ok=dlHARequest(String("/api/services/light/")+(operation=="off"?"turn_off":"turn_on"),"POST",body,response,code);
  if(ok){
    dlLastAction="Bulb "+String(i+1)+": "+operation;
    // Home Assistant usually returns changed states. Remember its context ID
    // so manual changes during a temporary effect do not get overwritten.
    DynamicJsonDocument result(4500);
    if(!deserializeJson(result,response) && result.is<JsonArray>()){
      for(JsonObjectConst changed:result.as<JsonArrayConst>()){
        if(String(changed["entity_id"]|"")==dlBulbConfig.entities[i]){
          dlTemporary[i].expectedContextId=String(changed["context"]["id"]|"");
          break;
        }
      }
    }
  }
  return ok;
}
bool dlScene(uint8_t i,const String& action){
  if(i>=DL_BULBS)return false;
  DynamicJsonDocument d(260);
  if(action=="create"){
    d["scene_id"]="doorlight_flash_before_"+String(i+1);
    d.createNestedArray("snapshot_entities").add(dlBulbConfig.entities[i]);
  } else {
    d["entity_id"]="scene.doorlight_flash_before_"+String(i+1);
    if(action=="turn_on")d["transition"]=2; // HA may honor light fade on scene restore
  }
  String body;serializeJson(d,body);String response;int code;
  return dlHARequest("/api/services/scene/"+action,"POST",body,response,code);
}
bool dlMeHome(bool& home) {
  if(dlBulbConfig.meEntity.length()==0||!dlHAReady())return false;
  DynamicJsonDocument d(1300);
  if(!dlReadEntity(dlBulbConfig.meEntity,d))return false;
  String s=d["state"]|"unknown";
  if(s=="home"){home=true;return true;}
  if(s=="not_home"){home=false;return true;}
  // Zone names, unavailable, and unknown cannot identify who crossed the door.
  return false;
}
int dlEffectivePct(int pct){
  return dlBrightnessPercent(pct,cfg.nightDimPercent,nightModeActive());
}
void dlFinishTemporary(uint8_t i){
  DlTemporary &t=dlTemporary[i];if(!t.active)return;
  DlBulbState current;
  bool readable=dlReadBulb(i,current);
  // Manual change protection. Only restore if the last commanded state remains.
  bool ours=readable && current.on==t.expectedOn &&
    (!t.expectedOn || abs(current.brightness-t.expectedBrightness)<=12);
  // When an HA command yields a context ID, it is stronger evidence than
  // RGB equality (Matter may report color using a different color space).
  if(readable && t.expectedContextId.length()>0 && current.contextId.length()>0){
    ours=(t.expectedContextId==current.contextId);
  }
  if(ours){
    bool restored=t.sceneSaved && dlScene(i,"turn_on");
    if(!restored){
      if(!t.before.on)restored=dlSendService(i,"off",-1,"",0,1);
      else restored=dlSendService(i,"on",max(1,(t.before.brightness*100+127)/255),
        t.before.colorMode=="rgb"?t.before.color:"",
        t.before.colorMode=="color_temp"?t.before.kelvin:0,1);
    }
    logLine(String("Bulb ")+String(i+1)+(restored?" restored":" restore failed"));
  } else logLine("Bulb "+String(i+1)+" restore skipped (manual change or offline)");
  if(t.sceneSaved)dlScene(i,"delete");
  t.active=false;
}
#include "BedroomSnapshotEngine.h"
void r4WeatherPlan(int& kelvin,String& color);
bool r4FingerprintCondition(bool own);
bool dlApplyActionToBulb(uint8_t i,const DlAction &a,bool applyNight=true){
  if(i>=2)return false;
  DlBulbState current;
  if(!dlReadBulb(i,current))return false; // Never overwrite unknown states.
  bool temporary=a.restore && a.durationSec>0;
  DlTemporary &t=dlTemporary[i];
  if(t.active && !temporary)t.active=false; // persistent manual/shortcut takes ownership
  if(temporary && !t.active){
    t.before=current;t.active=true;t.expectedContextId="";
    t.sceneSaved=dlScene(i,"create");
  }
  bool turnOn=a.operation!="off";
  if(a.operation=="toggle")turnOn=!current.on;
  int pct=applyNight?dlEffectivePct(a.brightness):a.brightness;
  if(turnOn && pct==0)turnOn=false;
  String op=turnOn?"on":"off";
  bool ok=false;
  if((a.operation=="sunrise" || a.operation=="weatherSunrise") && turnOn){
    int targetKelvin=a.kelvin>0?a.kelvin:4200;String targetColor=a.color;
    if(a.operation=="weatherSunrise")r4WeatherPlan(targetKelvin,targetColor);
    ok=dlSendService(i,"on",max(1,min(10,pct)),"",2200,1);
    if(ok)ok=dlSendService(i,"on",pct,"",targetKelvin,
                                         max(60,a.transitionSec));
  } else ok=dlSendService(i,op,pct,a.color,a.kelvin,a.operation=="pulse"?1:a.transitionSec);
  if(!ok){if(temporary){if(t.sceneSaved)dlScene(i,"delete");t.active=false;}return false;}
  if(temporary){
    t.untilMs=millis()+1000UL*a.durationSec;
    t.expectedOn=turnOn;
    t.expectedBrightness=(pct*255+50)/100;
    t.expectedColor=(a.kelvin==0)?a.color:"";
    t.pulse=a.operation=="pulse";
    t.pulseIsOn=turnOn;t.lastPulseMs=millis();t.pulseBrightness=pct;t.pulseColor=a.color;
  }
  return true;
}
bool dlApplyAction(const DlAction &a,bool applyNight=true){
  bool ok=true;
  if(a.target=="both"||a.target=="bulb1")ok=dlApplyActionToBulb(0,a,applyNight)&&ok;
  if(a.target=="both"||a.target=="bulb2")ok=dlApplyActionToBulb(1,a,applyNight)&&ok;
  if(a.target=="bedroom"){
    bool queued=bedroomQueueAction(a,applyNight);
    if(!queued)dlLastError="Bedroom effect not queued: "+bedroom.error;
    ok=queued&&ok;
  }
  if(a.target=="indicator"){
    dlIndicator.active=true;dlIndicator.startMs=millis();
    dlIndicator.durationMs=1000UL*constrain((int)(a.durationSec?a.durationSec:5),1,60);
    dlIndicator.brightness=constrain(a.brightness*255/100,0,255);
    dlIndicator.periodMs=(a.operation=="pulse")?1400:100000;
  }
  return ok;
}
void dlIndicatorTick(){
  if(!dlIndicator.active)return;
  uint32_t elapsed=millis()-dlIndicator.startMs;
  if(elapsed>=dlIndicator.durationMs){dlIndicator.active=false;writeIndicator(0);return;}
  uint32_t half=max(1UL,(uint32_t)dlIndicator.periodMs/2);
  uint32_t phase=elapsed%(uint32_t)dlIndicator.periodMs;
  if(phase>=half){writeIndicator(0);return;}
  auto ease=[](float x)->float{x=constrain(x,0.0f,1.0f);return x*x*(3.0f-2.0f*x);};
  float level=ease((float)phase/min(250UL,half))*
              ease((float)(half-phase)/min(250UL,half))*
              ease((float)(dlIndicator.durationMs-elapsed)/250.0f);
  writeIndicator((uint8_t)lroundf(nightAdjustedBrightness(dlIndicator.brightness)*level));
}
void dlTemporaryTick(){
  for(int i=0;i<2;++i){DlTemporary&t=dlTemporary[i];if(!t.active)continue;
    if((int32_t)(millis()-t.untilMs)>=0){dlFinishTemporary(i);continue;}
    if(t.pulse && (uint32_t)(millis()-t.lastPulseMs)>=1200){
      t.lastPulseMs=millis();t.pulseIsOn=!t.pulseIsOn;
      bool ok=dlSendService(i,t.pulseIsOn?"on":"off",t.pulseBrightness,t.pulseColor,0,1);
      if(ok){t.expectedOn=t.pulseIsOn;t.expectedBrightness=t.pulseIsOn?(t.pulseBrightness*255+50)/100:0;t.expectedColor=t.pulseIsOn?t.pulseColor:"";}
    }
  }
}
void dlWriteAction(JsonObject o,const DlAction&a){
  o["target"]=a.target;o["operation"]=a.operation;o["color"]=a.color;
  o["brightness"]=a.brightness;o["kelvin"]=a.kelvin;
  o["durationSec"]=a.durationSec;o["transitionSec"]=a.transitionSec;o["restore"]=a.restore;
  o["stripMode"]=a.stripMode;o["stripHalfMs"]=a.stripHalfMs;
}
void dlWriteRule(JsonObject o,const DlRule&r){
  o["id"]=r.id;o["name"]=r.name;o["enabled"]=r.enabled;o["trigger"]=r.trigger;
  o["condition"]=r.condition;o["time"]=r.time;o["days"]=r.days;
  o["everyMinutes"]=r.everyMinutes;o["cooldownSec"]=r.cooldownSec;
  JsonArray a=o.createNestedArray("actions");
  for(int k=0;k<r.actionCount;k++)dlWriteAction(a.createNestedObject(),r.actions[k]);
}
bool dlParseAction(JsonObjectConst d,DlAction& a){
  if(d.isNull())return false;
  a.target=String(d["target"]|"both");a.operation=String(d["operation"]|"on");
  a.color=String(d["color"]|"FFCA8D");a.color.toUpperCase();
  a.brightness=d["brightness"]|60;a.kelvin=d["kelvin"]|0;
  a.durationSec=d["durationSec"]|0;a.transitionSec=d["transitionSec"]|1;
  a.restore=d["restore"]|false;
  a.stripMode=String(d["stripMode"]|"");a.stripHalfMs=d["stripHalfMs"]|600;
  if(a.stripMode.length()&&!bedroomValidMode(a.stripMode))return false;
  if(a.stripHalfMs<100||a.stripHalfMs>5000)return false;
  if(a.target=="bedroom"&&(!a.restore||a.durationSec<1||a.durationSec>600||
      (a.kelvin!=0&&(a.kelvin<2700||a.kelvin>6500))))return false;
  if(!dlInList(a.target,DL_TARGETS)||!dlInList(a.operation,DL_OPERATIONS)||!validColor(a.color))return false;
  if(a.brightness<0||a.brightness>100||a.kelvin<0||a.kelvin>6500||
     (a.kelvin>0&&a.kelvin<1800)||a.durationSec<0||a.durationSec>86400||a.transitionSec<0||a.transitionSec>900)return false;
  if(a.operation=="pulse" && (!a.restore||a.durationSec==0))return false;
  return true;
}
bool dlValidId(const String& id){
  if(id.length()<1||id.length()>14)return false;
  for(size_t i=0;i<id.length();++i){char c=id[i];if(!(islower((unsigned char)c)||isdigit((unsigned char)c)||c=='-'))return false;}
  return true;
}
bool dlParseRule(JsonObjectConst d,DlRule& r){
  r.id=String(d["id"]|"");r.name=String(d["name"]|"");
  r.trigger=String(d["trigger"]|"manual");r.condition=String(d["condition"]|"any");
  r.time=String(d["time"]|"07:00");r.enabled=d["enabled"]|true;
  int days=d["days"]|127;int every=d["everyMinutes"]|0;int cooldown=d["cooldownSec"]|15;
  if(!dlValidId(r.id)||r.name.length()<1||r.name.length()>64||!dlInList(r.trigger,DL_TRIGGERS)||
      !dlInList(r.condition,DL_CONDITIONS)||!validTime(r.time)||days<0||days>127||
      every<0||every>1440||cooldown<0||cooldown>3600)return false;
  if(r.trigger=="interval"&&every==0)return false;
  r.days=days;r.everyMinutes=every;r.cooldownSec=cooldown;
  JsonArrayConst actions=d["actions"].as<JsonArrayConst>();
  if(actions.size()<1||actions.size()>DL_MAX_ACTIONS)return false;
  r.actionCount=actions.size();
  for(uint8_t i=0;i<r.actionCount;++i)if(!dlParseAction(actions[i],r.actions[i]))return false;
  return true;
}
int dlFindRule(const String& id){for(int i=0;i<dlRuleCount;++i)if(dlRules[i].id==id)return i;return -1;}
void dlAddPreset(const String& id,const String& name,const String& trigger,const String& condition,
                 const String& operation,const String& target,const String& color,int brightness,
                 int durationSec,bool restore,bool enabled=true,const String& time="07:00",int days=127,int transition=1){
  if(dlRuleCount>=DL_MAX_RULES)return;
  DlRule &r=dlRules[dlRuleCount++];r=DlRule();r.id=id;r.name=name;r.trigger=trigger;r.condition=condition;
  r.enabled=enabled;r.time=time;r.days=days;r.cooldownSec=15;r.actionCount=1;
  DlAction &a=r.actions[0];a.target=target;a.operation=operation;a.color=color;a.brightness=brightness;
  a.durationSec=durationSec;a.restore=restore;a.transitionSec=transition;
}
void dlDefaults(){
  dlRuleCount=0;
  dlAddPreset("alarm-week","Weekday sunrise","time","any","weatherSunrise","both","FFCD99",65,0,false,true,"07:00",31,900);
  dlAddPreset("alarm-weekend","Weekend gentle wake","time","any","weatherSunrise","both","FFD6A4",45,0,false,true,"09:00",96,900);
  dlAddPreset("guest-entry","Welcome someone else","entry","fingerprintOther","on","both","FFE2AE",62,0,false,true);
  dlAddPreset("guest-exit","Roommate leaves","exit","meAway","off","both","000000",0,0,false,false);
  dlAddPreset("night-path","Quiet nighttime pass","motion","night","pulse","both","FFAD5B",14,4,true,true);
  dlAddPreset("visitor","Visitor at the door","loiter","any","pulse","both","B68DFF",38,6,true,true);
  dlAddPreset("walkby","Walk-by shimmer","walkby","any","pulse","both","6EC8FF",21,3,true,false);
  dlAddPreset("door-open","Door left open reminder","door_left_open","any","pulse","both","FF635B",35,7,true,false);
  dlAddPreset("evening","Evening amber scene","time","any","fade","both","FFB76A",30,0,false,false,"18:00",127,8);
  dlAddPreset("study","Study focus","manual","any","on","both","D5EFFF",85,2700,true,true);
  dlAddPreset("movie","Movie time","manual","any","fade","both","6054CC",12,7200,true,true,"07:00",127,4);
  dlAddPreset("game-start","Gaming atmosphere","gaming_start","any","fade","both","5366C8",18,0,false,false);
  dlAddPreset("pomodoro-break","Pomodoro break","pomodoro_break","any","fade","both","B4DB88",35,0,false,false);
  dlAddPreset("status-test","Blue notification","manual","any","pulse","bulb1","62BDFB",25,4,true,true);
}
bool dlSaveRules(){
  if(!dlFsReady)return false;
  DynamicJsonDocument d(20000);d["schema"]=1;
  JsonArray rules=d.createNestedArray("rules");for(int i=0;i<dlRuleCount;++i)dlWriteRule(rules.createNestedObject(),dlRules[i]);
  const char* tmp="/doorlight-shortcuts.json.tmp";
  const char* backup="/doorlight-shortcuts.json.bak";
  File f=LittleFS.open(tmp,"w");if(!f)return false;
  size_t wrote=serializeJson(d,f);f.close();if(!wrote)return false;
  // Keep a previous working copy until the new one is in place.
  LittleFS.remove(backup);
  if(LittleFS.exists(DL_FILE))LittleFS.rename(DL_FILE,backup);
  if(LittleFS.rename(tmp,DL_FILE))return true;
  if(LittleFS.exists(backup))LittleFS.rename(backup,DL_FILE);
  return false;
}
void dlLoadRules(){
  dlDefaults();if(!dlFsReady)return;
  File f=LittleFS.open(DL_FILE,"r");
  if(!f)f=LittleFS.open("/doorlight-shortcuts.json.bak","r");
  if(!f){dlSaveRules();return;}
  DynamicJsonDocument d(22000);DeserializationError err=deserializeJson(d,f);f.close();
  if(err || (int)(d["schema"]|0)!=1)return;
  JsonArrayConst rs=d["rules"].as<JsonArrayConst>();if(rs.size()>DL_MAX_RULES)return;
  static DlRule loaded[DL_MAX_RULES];uint8_t count=0;
  for(JsonObjectConst obj:rs){if(!dlParseRule(obj,loaded[count]))return;count++;}
  for(int i=0;i<count;i++){for(int j=0;j<i;j++)if(loaded[i].id==loaded[j].id)return;}
  dlRuleCount=count;
  for(int i=0;i<count;++i)dlRules[i]=loaded[i];
}
void dlSaveConfig(){
  StaticJsonDocument<750>d;d["enabled"]=dlBulbConfig.enabled;d["host"]=dlBulbConfig.host;
  d["port"]=dlBulbConfig.port;d["entity1"]=dlBulbConfig.entities[0];d["entity2"]=dlBulbConfig.entities[1];
  d["label1"]=dlBulbConfig.labels[0];d["label2"]=dlBulbConfig.labels[1];d["meEntity"]=dlBulbConfig.meEntity;
  String s;serializeJson(d,s);prefs.putString("haConfig",s);
}
void dlLoadConfig(){
  String data=prefs.getString("haConfig","");if(data.length()==0)return;
  StaticJsonDocument<750>d;if(deserializeJson(d,data))return;
  String host=d["host"]|"";int port=d["port"]|8123;
  String e1=d["entity1"]|"",e2=d["entity2"]|"",person=d["meEntity"]|"";
  if(validHost(host))dlBulbConfig.host=host;
  if(port>=1&&port<=65535)dlBulbConfig.port=port;
  if(dlValidEntity(e1,"light"))dlBulbConfig.entities[0]=e1;
  if(dlValidEntity(e2,"light"))dlBulbConfig.entities[1]=e2;
  if(person.length()==0||dlValidEntity(person,"person")||dlValidEntity(person,"device_tracker"))dlBulbConfig.meEntity=person;
  for(int i=0;i<2;++i){String v=d[i==0?"label1":"label2"]|"";if(v.length()<=32&&v.length()>0)dlBulbConfig.labels[i]=v;}
  dlBulbConfig.enabled=d["enabled"]|false;
}
void dlWriteBulbConfig(JsonObject o){
  o["enabled"]=dlBulbConfig.enabled;o["host"]=dlBulbConfig.host;o["port"]=dlBulbConfig.port;
  o["entity1"]=dlBulbConfig.entities[0];o["entity2"]=dlBulbConfig.entities[1];
  o["label1"]=dlBulbConfig.labels[0];o["label2"]=dlBulbConfig.labels[1];o["meEntity"]=dlBulbConfig.meEntity;
  o["connected"]=dlHAReady();o["tokenConfigured"]=strlen(BRIDGE_HA_TOKEN)>30 && !String(BRIDGE_HA_TOKEN).startsWith("REPLACE_");
}
void dlWriteBulbState(JsonObject o,uint8_t i){
  const DlBulbState &b=dlBulbs[i];o["id"]=i+1;o["label"]=dlBulbConfig.labels[i];o["entity"]=dlBulbConfig.entities[i];
  o["valid"]=b.valid;o["on"]=b.on;o["brightness"]=b.brightness;
  o["brightnessPct"]=(b.brightness*100+127)/255;o["color"]=b.color;
  o["kelvin"]=b.kelvin;o["colorMode"]=b.colorMode;
  o["error"]=b.error;o["ageMs"]=millis()-b.checkedAt;
  o["effectActive"]=dlTemporary[i].active;
}
void dlHandleBulbs(){
  if(!authorized())return;
  DynamicJsonDocument d(1300);d["ok"]=true;d["lastAction"]=dlLastAction;d["lastError"]=dlLastError;
  dlWriteBulbConfig(d.createNestedObject("config"));JsonArray bulbs=d.createNestedArray("bulbs");
  for(int i=0;i<2;i++)dlWriteBulbState(bulbs.createNestedObject(),i);
  bool home=false;d["meHomeKnown"]=dlMeHome(home);d["meHome"]=home;
  jsonReply(200,d);
}
void dlHandleBulbConfig(){
  if(!authorized())return;
  StaticJsonDocument<3072>d;if(!parseBody(d))return;
  DlBulbConfig next=dlBulbConfig;
  next.enabled=d["enabled"]|next.enabled;
  next.host=String(d["host"]|next.host);
  int port=d["port"]|(int)next.port;
  if(port<1||port>65535){reply(400,"Home Assistant port must be 1..65535");return;}
  next.port=port;
  next.entities[0]=String(d["entity1"]|next.entities[0]);next.entities[1]=String(d["entity2"]|next.entities[1]);
  next.labels[0]=String(d["label1"]|next.labels[0]);next.labels[1]=String(d["label2"]|next.labels[1]);
  next.meEntity=String(d["meEntity"]|next.meEntity);
  if(!validHost(next.host)||next.port<1||!dlValidEntity(next.entities[0],"light")||!dlValidEntity(next.entities[1],"light")||
     next.entities[0]==next.entities[1]||next.labels[0].length()>32||next.labels[1].length()>32||
     !(next.meEntity.length()==0||dlValidEntity(next.meEntity,"person")||dlValidEntity(next.meEntity,"device_tracker"))){
    reply(400,"Invalid HA host, port, bulb entity IDs, labels, or person entity");return;
  }
  dlBulbConfig=next;dlSaveConfig();
  for(auto& b:dlBulbs)b.valid=false;
  dlLastBulbPoll=millis()-12000;reply(200,"Smart bulb settings saved");
}
void dlHandleBulbControl(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  DlAction a; a.target=String(d["target"]|"both");a.operation=String(d["operation"]|"on");
  a.color=String(d["color"]|"FFCA8D");a.color.toUpperCase();a.brightness=d["brightness"]|60;
  a.kelvin=d["kelvin"]|0;a.transitionSec=d["transitionSec"]|1;a.durationSec=d["durationSec"]|0;a.restore=d["restore"]|false;
  if(!dlParseAction(d.as<JsonObjectConst>(),a) || !dlHAReady()){reply(400,"Invalid bulb command or Home Assistant not configured");return;}
  if(a.target=="bedroom"||a.target=="indicator"){reply(400,"Direct bulb control expects both/bulb1/bulb2");return;}
  // Direct user controls are not reduced by scheduled Night Mode.
  if(dlApplyAction(a,false))reply(200,"Home Assistant accepted bulb command; read-back happens on next refresh");
  else reply(502,"Home Assistant failed to apply bulb action; check /api/automation/status");
}
void dlHandleShortcuts(){
  if(!authorized())return;DynamicJsonDocument d(22000);d["ok"]=true;d["maxRules"]=DL_MAX_RULES;
  JsonArray rules=d.createNestedArray("rules");for(int i=0;i<dlRuleCount;i++)dlWriteRule(rules.createNestedObject(),dlRules[i]);
  jsonReply(200,d);
}
void dlHandleShortcutSave(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  DlRule r;if(!dlParseRule(d.as<JsonObjectConst>(),r)){reply(400,"Invalid shortcut fields, action count, or schedule");return;}
  int idx=dlFindRule(r.id);
  if(idx<0&&dlRuleCount>=DL_MAX_RULES){reply(409,"Shortcut limit reached");return;}
  if(idx<0)idx=dlRuleCount++;
  dlRules[idx]=r;if(!dlSaveRules()){reply(500,"Shortcut changed in RAM but not saved to LittleFS");return;}
  reply(200,"Shortcut saved");
}
void dlHandleShortcutDelete(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String id=d["id"]|"";int i=dlFindRule(id);
  if(i<0){reply(404,"Unknown shortcut");return;}
  for(int j=i;j<dlRuleCount-1;++j)dlRules[j]=dlRules[j+1];dlRuleCount--;
  if(!dlSaveRules()){reply(500,"Deletion not persisted");return;}
  reply(200,"Shortcut deleted");
}
bool dlCondition(const String &cond){
  if(cond=="any")return true;
  if(cond=="night")return nightModeActive();
  if(cond=="day")return !nightModeActive()&&timeAvailable();
  if(cond=="roommates")return lockConnected&&lockMode=="roommates";
  if(cond=="everyone")return lockConnected&&lockMode=="everyone";
  if(cond=="fingerprintMe")return r4FingerprintCondition(true);
  if(cond=="fingerprintOther")return r4FingerprintCondition(false);
  if(cond=="meHome"||cond=="meAway"){
    bool home=false;
    // Presence unknown = NO automation (don't infer individual identity).
    if(!dlMeHome(home))return false;
    return cond=="meHome"?home:!home;
  }
  return false;
}
bool dlExecuteRule(uint8_t i,bool force=false){
  if(i>=dlRuleCount)return false;
  DlRule &r=dlRules[i];
  if(!r.enabled&&!force)return false;
  if(!force && r.lastEventMs!=0 && (uint32_t)(millis()-r.lastEventMs)<1000UL*r.cooldownSec)return false;
  if(!dlCondition(r.condition)){logLine("Shortcut skipped (condition unavailable): "+r.name);return false;}
  r.lastEventMs=millis();r.lastEpochRun=(uint32_t)time(nullptr);
  logLine("Shortcut: "+r.name);
  bool ok=true;
  for(int a=0;a<r.actionCount;a++)ok=dlApplyAction(r.actions[a])&&ok;
  if(!ok){dlLastError="Shortcut partially failed: "+r.name;logLine(dlLastError);}
  return ok;
}
String dlTriggerQueue[10];
uint8_t dlQueueHead=0,dlQueueTail=0;
void dlAutomationTrigger(const String& trigger){
  uint8_t next=(dlQueueTail+1)%10;
  if(next==dlQueueHead){logLine("Automation queue full; event dropped");return;}
  dlTriggerQueue[dlQueueTail]=trigger;dlQueueTail=next;
}
void dlDrainTrigger(){
  if(dlQueueHead==dlQueueTail)return;
  String trigger=dlTriggerQueue[dlQueueHead];dlQueueHead=(dlQueueHead+1)%10;
  if(r4GamingSuppresses(trigger))return;
  for(int i=0;i<dlRuleCount;i++)if(dlRules[i].trigger==trigger)dlExecuteRule(i);
}
uint32_t dlDayStamp(const struct tm& t){return (uint32_t)(t.tm_year+1900)*10000U+(t.tm_mon+1)*100U+t.tm_mday;}
void dlAutomationTick(){
  uint32_t now=millis();
  dlDrainTrigger();
  dlTemporaryTick();
  dlIndicatorTick();
  if(dlCountdown.active && (int32_t)(now-dlCountdown.due)>=0){
    int i=dlFindRule(dlCountdown.id);dlCountdown.active=false;
    if(i>=0)dlExecuteRule(i,true);
  }
  if((uint32_t)(now-dlLastRuleTick)<1000)return;
  dlLastRuleTick=now;
  if(timeAvailable()){
    time_t epoch=time(nullptr);struct tm t;localtime_r(&epoch,&t);
    uint8_t dayBit=dlWeekdayBit(t.tm_wday);
    char clockBuf[6];snprintf(clockBuf,sizeof(clockBuf),"%02d:%02d",t.tm_hour,t.tm_min);
    String clock=clockBuf;
    for(int i=0;i<dlRuleCount;i++){
      DlRule&r=dlRules[i];if(!r.enabled)continue;
      if(r.trigger=="time"&&r.time==clock&&(r.days&dayBit)){
        uint32_t stamp=dlDayStamp(t);
        String key="r"+r.id; // NVS key length <= 15
        if(prefs.getUInt(key.c_str(),0)==stamp)continue;
        // Mark once even when a condition is unavailable; avoids minute-long storms.
        prefs.putUInt(key.c_str(),stamp);
        dlExecuteRule(i);
      }
      if(r.trigger=="interval" && r.everyMinutes>0 &&
         (uint32_t)(now-r.lastEventMs)>=60000UL*r.everyMinutes)dlExecuteRule(i);
    }
  }
  // Read both actual bulb states every 7 seconds when available.
  if(dlHAReady()&&(uint32_t)(now-dlLastBulbPoll)>=7000){
    dlLastBulbPoll=now;for(int i=0;i<2;i++)if(!dlTemporary[i].active)dlReadBulb(i,dlBulbs[i]);
  }
}
void dlHandleShortcutRun(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String id=d["id"]|"";int i=dlFindRule(id);
  if(i<0){reply(404,"Unknown shortcut");return;}
  if(!dlExecuteRule(i,true)){reply(409,"Shortcut condition unavailable or an action failed; see /api/automation/status");return;}
  reply(202,"Shortcut dispatched");
}
void dlHandleCountdown(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String id=d["id"]|"";int seconds=d["delaySeconds"]|0;
  if(dlFindRule(id)<0||seconds<1||seconds>86400){reply(400,"Use valid rule ID and delaySeconds 1..86400");return;}
  dlCountdown.active=true;dlCountdown.id=id;dlCountdown.due=millis()+1000UL*seconds;
  reply(200,"Countdown started (RAM only; reboot cancels)");
}
void dlHandleAutomationStatus(){
  if(!authorized())return;StaticJsonDocument<550>d;d["ok"]=true;d["countdownActive"]=dlCountdown.active;
  d["countdownId"]=dlCountdown.id;d["countdownRemainingSec"]=dlCountdown.active?(int32_t)(dlCountdown.due-millis())/1000:0;
  d["rules"]=dlRuleCount;d["storageReady"]=dlFsReady;d["lastAction"]=dlLastAction;d["lastError"]=dlLastError;
  jsonReply(200,d);
}
void dlSetupRoutes(){
  server.on("/api/bulbs",HTTP_GET,dlHandleBulbs);
  server.on("/api/bulbs/config",HTTP_PUT,dlHandleBulbConfig);
  server.on("/api/bulbs/control",HTTP_POST,dlHandleBulbControl);
  server.on("/api/shortcuts",HTTP_GET,dlHandleShortcuts);
  server.on("/api/shortcuts",HTTP_PUT,dlHandleShortcutSave);
  server.on("/api/shortcuts",HTTP_DELETE,dlHandleShortcutDelete);
  server.on("/api/shortcuts/run",HTTP_POST,dlHandleShortcutRun);
  server.on("/api/shortcuts/countdown",HTTP_POST,dlHandleCountdown);
  server.on("/api/automation/status",HTTP_GET,dlHandleAutomationStatus);
  bedroomSetupRoutes();
}
