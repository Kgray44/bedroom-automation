#pragma once
#include "BedroomRestorePolicy.h"
// Doorlight v5: reversible bedroom-strip effects using ONLY the existing
// Bedroom_Lights ESP32-S3 HTTP routes. Does not require a firmware patch.
// Included by SmartAutomations.h after DlAction and dlEffectivePct are defined.

static const char* BEDROOM_NVS_KEY = "roomRestore"; // Preferences key <= 15 chars
static constexpr uint8_t BEDROOM_QUEUE_MAX = 5;
static constexpr uint32_t BEDROOM_RETRY_PAUSE_MS = 1800;

enum BedroomStage : uint8_t {
  BED_IDLE, BED_CAPTURE, BED_APPLY, BED_CHECK_APPLY, BED_ACTIVE,
  BED_PRE_RESTORE, BED_RESTORE_FADE_SETUP, BED_RESTORE_BASE, BED_RESTORE_PALETTE,
  BED_RESTORE_TRANSITIONS, BED_VERIFY, BED_FAILED
};

struct BedroomSnapshot {
  String mode="", hex="", paletteId="", transitionStyle="fade";
  bool white=false, paletteEnabled=false, transitionsEnabled=false;
  bool nightGuardEnabled=false, nightGuardBlockFlashing=false;
  bool timerActive=false, testActive=false, transitionActive=false;
  int kelvin=3000, brightness=0, strength=255, transitionDuration=0;
  int strobeTenths=10, flashTenths=10, chase=5, wave=10;
  int slowPulseCount=3, slowPulseMax=5, rainbow=10, candleFlicker=0;
};
struct BedroomEffect {
  String source="",mode="solid",color="FFB85B";
  int kelvin=0, brightness=32, halfMs=600;
  uint32_t durationMs=4000;
  uint8_t priority=1;
};
struct BedroomRuntime {
  BedroomStage stage=BED_IDLE;
  BedroomSnapshot before, expected, observed;
  BedroomEffect current;
  BedroomEffect queue[BEDROOM_QUEUE_MAX];
  uint8_t count=0, restoreAttempts=0, applyAttempts=0;
  bool snapshotPresent=false, expectedValid=false, forceRestore=false;
  uint32_t due=0, expires=0, lastReadAt=0, fadeEndAt=0;
  String result="No bedroom effects yet", error="", lastSource="";
  int lastHttp=0;
} bedroom;

// Native fades are supported by the existing bedroom controller. They are
// separate from its saved ambient transition preference, which is restored.
struct BedroomFadeConfig { bool enabled=true; uint16_t fadeInMs=1500, fadeOutMs=1500; } bedroomFades;
static constexpr const char* BEDROOM_FADES_NVS_KEY = "roomFades";
bool bedroomValidFade(int ms){return ms==0||ms==500||ms==1500||ms==3000||ms==5000;}
void bedroomLoadFades(){
  String raw=prefs.getString(BEDROOM_FADES_NVS_KEY, "");
  if(raw.isEmpty())return;
  StaticJsonDocument<256> d;
  if(deserializeJson(d,raw))return;
  int in=d["fadeInMs"]|1500, out=d["fadeOutMs"]|1500;
  if(bedroomValidFade(in)&&bedroomValidFade(out)){
    bedroomFades.enabled=d["enabled"]|true;
    bedroomFades.fadeInMs=in;bedroomFades.fadeOutMs=out;
  }
}
void bedroomSaveFades(){
  StaticJsonDocument<256> d;d["enabled"]=bedroomFades.enabled;
  d["fadeInMs"]=bedroomFades.fadeInMs;d["fadeOutMs"]=bedroomFades.fadeOutMs;
  String out;serializeJson(d,out);prefs.putString(BEDROOM_FADES_NVS_KEY,out);
}
const char* bedroomStageName(BedroomStage stage) {
  switch(stage) {
    case BED_IDLE:return "idle";case BED_CAPTURE:return "capturing";
    case BED_APPLY:return "applying";case BED_CHECK_APPLY:return "confirming-effect";
    case BED_ACTIVE:return "active";case BED_PRE_RESTORE:return "checking-ownership";
    case BED_RESTORE_FADE_SETUP:return "preparing-restore-fade";case BED_RESTORE_BASE:return "restoring-state";
    case BED_RESTORE_PALETTE:return "restoring-palette";
    case BED_RESTORE_TRANSITIONS:return "restoring-transitions";
    case BED_VERIFY:return "verifying";case BED_FAILED:return "failed";
  }
  return "unknown";
}

bool bedroomValidToken(const String& value, uint8_t limit=35) {
  if(value.isEmpty()||value.length()>limit)return false;
  for(size_t i=0;i<value.length();i++){
    char c=value[i];
    if(!isalnum((unsigned char)c)&&c!='-'&&c!='_')return false;
  }
  return true;
}
bool bedroomValidMode(const String& mode){
  static const char* modes[]={"solid","strobe","flash","chase","wave","slowWave",
    "slowPulse","rainbow","dawnBloom","moonTide","velvetAurora","lanternDrift",
    "rainGlass","satinBreathing","paperLanternRow","libraryLamp","solarNoon",
    "goldenHour","dreamAquarium","circuitGlow","windowSunrise","emberQuilt",
    "porcelainSky","hearthline","eveningCurrent","starlitLinen","deepSeaGlass",
    "quietNorthernLights","candleHall","slowPrism","gardenShade","snowfallHush",
    "observatoryGlow","rainyWindow","campfireBlanket","northernWhisper","stormOutside"};
  for(const char* name:modes)if(mode==name)return true;
  return false;
}
String bedroomHex(String value){
  if(value.startsWith("#"))value.remove(0,1);
  value.toUpperCase();return value;
}
String bedroomFraction(int tenths){return String((float)tenths/10.0f,1);}
int bedroomPercentToByte(int percent){return constrain((percent*255+50)/100,0,255);}

bool bedroomReadSnapshot(JsonVariantConst d,BedroomSnapshot& s){
  if(!d.is<JsonObjectConst>() || (d["ok"].is<bool>()&&!d["ok"].as<bool>()))return false;
  String mode=d["mode"]|"",hex=bedroomHex(String(d["hex"]|""));
  if(!bedroomValidMode(mode)||!validColor(hex)||!d["masterBrightness"].is<int>())return false;
  // Refuse incomplete snapshots. Defaulting a missing value can destroy the
  // user's pre-alert animation configuration during the later restore.
  static const char* required[]={"usingWhiteTemperature","kelvin","animationStrength",
    "paletteEnabled","activePaletteId","transitionsEnabled","transitionDurationMs",
    "transitionStyle","strobeDelay","flashOffDelay","chasePeriod","wavePeriod",
    "slowPulseCount","slowPulseMax","rainbowPeriod","candleHallFlickerAmount",
    "timerActive","testActive","transitionActive"};
  for(const char* field:required)if(d[field].isNull())return false;
  s.mode=mode;s.hex=hex;
  s.white=d["usingWhiteTemperature"]|false;
  s.kelvin=constrain((int)(d["kelvin"]|3000),2700,6500);
  s.brightness=constrain((int)(d["masterBrightness"]|0),0,255);
  s.strength=constrain((int)(d["animationStrength"]|255),0,255);
  s.paletteEnabled=d["paletteEnabled"]|false;
  s.paletteId=String(d["activePaletteId"]|"");
  if(s.paletteId.length() && !bedroomValidToken(s.paletteId,30))return false;
  s.transitionsEnabled=d["transitionsEnabled"]|false;
  s.transitionDuration=constrain((int)(d["transitionDurationMs"]|0),0,5000);
  s.transitionStyle=String(d["transitionStyle"]|"fade");
  if(s.transitionStyle!="fade")return false;
  s.strobeTenths=constrain((int)lroundf(10.0f*(float)(d["strobeDelay"]|1.0f)),1,100);
  s.flashTenths=constrain((int)lroundf(10.0f*(float)(d["flashOffDelay"]|1.0f)),1,100);
  s.chase=constrain((int)(d["chasePeriod"]|5),1,15);
  s.wave=constrain((int)(d["wavePeriod"]|10),5,30);
  s.slowPulseCount=constrain((int)(d["slowPulseCount"]|3),1,15);
  s.slowPulseMax=constrain((int)(d["slowPulseMax"]|5),1,60);
  s.rainbow=constrain((int)(d["rainbowPeriod"]|10),1,30);
  s.candleFlicker=constrain((int)(d["candleHallFlickerAmount"]|0),0,20);
  s.nightGuardEnabled=d["nightGuardEnabled"]|false;
  s.nightGuardBlockFlashing=d["nightGuardBlockFlashingModes"]|false;
  s.timerActive=d["timerActive"]|false;
  s.testActive=d["testActive"]|false;
  s.transitionActive=d["transitionActive"]|false;
  return true;
}
void bedroomWriteSnapshot(JsonObject d,const BedroomSnapshot& s){
  d["mode"]=s.mode;d["hex"]=s.hex;d["white"]=s.white;d["kelvin"]=s.kelvin;
  d["brightness"]=s.brightness;d["strength"]=s.strength;
  d["paletteEnabled"]=s.paletteEnabled;d["paletteId"]=s.paletteId;
  d["transitionsEnabled"]=s.transitionsEnabled;
  d["transitionDuration"]=s.transitionDuration;d["transitionStyle"]=s.transitionStyle;
  d["strobeTenths"]=s.strobeTenths;d["flashTenths"]=s.flashTenths;
  d["chase"]=s.chase;d["wave"]=s.wave;d["slowPulseCount"]=s.slowPulseCount;
  d["slowPulseMax"]=s.slowPulseMax;d["rainbow"]=s.rainbow;d["candleFlicker"]=s.candleFlicker;
}
bool bedroomDecodeSnapshot(JsonVariantConst d,BedroomSnapshot &s){
  if(!d.is<JsonObjectConst>())return false;
  s.mode=String(d["mode"]|"");s.hex=String(d["hex"]|"");
  if(!bedroomValidMode(s.mode)||!validColor(s.hex))return false;
  s.white=d["white"]|false;s.kelvin=d["kelvin"]|3000;
  s.brightness=d["brightness"]|0;s.strength=d["strength"]|255;
  s.paletteEnabled=d["paletteEnabled"]|false;s.paletteId=String(d["paletteId"]|"");
  s.transitionsEnabled=d["transitionsEnabled"]|false;
  s.transitionDuration=d["transitionDuration"]|0;
  s.transitionStyle=String(d["transitionStyle"]|"fade");
  s.strobeTenths=d["strobeTenths"]|10;s.flashTenths=d["flashTenths"]|10;
  s.chase=d["chase"]|5;s.wave=d["wave"]|10;
  s.slowPulseCount=d["slowPulseCount"]|3;s.slowPulseMax=d["slowPulseMax"]|5;
  s.rainbow=d["rainbow"]|10;s.candleFlicker=d["candleFlicker"]|0;
  if(s.brightness<0||s.brightness>255||s.strength<0||s.strength>255||
    s.strobeTenths<1||s.strobeTenths>100||s.flashTenths<1||s.flashTenths>100||
    s.transitionDuration<0||s.transitionDuration>5000||s.transitionStyle!="fade"||
    (s.paletteId.length()&&!bedroomValidToken(s.paletteId,30)))return false;
  return true;
}

bool bedroomEqual(const BedroomSnapshot& a,const BedroomSnapshot& b,bool full){
  // Color-temperature mode converts Kelvin to RGB internally, so the
  // controller's derived RGB hex is not an independent source of truth.
  bool basic=a.mode==b.mode && a.white==b.white && a.brightness==b.brightness &&
    (a.white?a.kelvin==b.kelvin:a.hex==b.hex);
  if(!basic)return false;
  if(!full)return a.mode!="strobe" || a.strobeTenths==b.strobeTenths;
  return a.strength==b.strength && a.strobeTenths==b.strobeTenths &&
    a.flashTenths==b.flashTenths && a.chase==b.chase && a.wave==b.wave &&
    a.slowPulseCount==b.slowPulseCount && a.slowPulseMax==b.slowPulseMax &&
    a.rainbow==b.rainbow && a.candleFlicker==b.candleFlicker &&
    a.paletteId==b.paletteId && a.paletteEnabled==b.paletteEnabled &&
    a.transitionsEnabled==b.transitionsEnabled && a.transitionDuration==b.transitionDuration &&
    a.transitionStyle==b.transitionStyle;
}

bool bedroomStoreRecovery(){
  if(!bedroom.snapshotPresent)return true;
  DynamicJsonDocument d(1700);d["version"]=5;
  d["expectedValid"]=bedroom.expectedValid;
  bedroomWriteSnapshot(d.createNestedObject("before"),bedroom.before);
  bedroomWriteSnapshot(d.createNestedObject("expected"),bedroom.expected);
  String serialized;serializeJson(d,serialized);
  return prefs.putString(BEDROOM_NVS_KEY,serialized)==serialized.length();
}
void bedroomDropRecovery(){
  prefs.remove(BEDROOM_NVS_KEY);
  bedroom.snapshotPresent=false;bedroom.expectedValid=false;bedroom.forceRestore=false;
}
void bedroomLoadRecovery(){
  String saved=prefs.getString(BEDROOM_NVS_KEY,"");if(saved.isEmpty())return;
  DynamicJsonDocument d(1800);
  if(deserializeJson(d,saved)|| (int)(d["version"]|0)!=5 ||
      !bedroomDecodeSnapshot(d["before"],bedroom.before)){
    bedroom.stage=BED_FAILED;bedroom.result="Damaged saved snapshot; manual inspection required";
    bedroom.error="Recovery snapshot is invalid; do not overwrite lighting";return;
  }
  bedroom.snapshotPresent=true;bedroom.expectedValid=d["expectedValid"]|false;
  if(bedroom.expectedValid&&!bedroomDecodeSnapshot(d["expected"],bedroom.expected))bedroom.expectedValid=false;
  bedroom.stage=bedroom.expectedValid?BED_PRE_RESTORE:BED_FAILED;
  bedroom.result=bedroom.expectedValid?"Reboot recovery: checking who owns the strip":"Recovery needs confirmation before changing strip";
  bedroom.due=millis()+1200;
}

bool bedroomHttpGet(const String& path,String* payload=nullptr){
  if(WiFi.status()!=WL_CONNECTED){bedroom.error="Doorlight Wi-Fi offline";return false;}
  WiFiClient client;HTTPClient http;
  http.setConnectTimeout(850);http.setTimeout(1750);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(client,"http://"+cfg.bedroomHost+path)){
    bedroom.error="Bedroom HTTP connection could not start";return false;
  }
  int code=http.GET();bedroom.lastHttp=code;
  bool ok=code==200;
  if(ok && payload){
    int len=http.getSize();
    if(len>7000){ok=false;bedroom.error="Bedroom state response too large";}
    else *payload=http.getString();
  }
  http.end();
  if(!ok&&bedroom.error.isEmpty())bedroom.error="Bedroom HTTP response: "+String(code);
  return ok;
}
bool bedroomRead(BedroomSnapshot &s){
  String response;
  if(!bedroomHttpGet("/api/state",&response))return false;
  DynamicJsonDocument d(7200);
  if(deserializeJson(d,response)||!bedroomReadSnapshot(d.as<JsonVariantConst>(),s)){
    bedroom.error="Bedroom /api/state did not include required lighting fields";return false;
  }
  bedroom.observed=s;bedroom.lastReadAt=millis();
  return true;
}
String bedroomBasePath(const BedroomSnapshot& s){
  String url="/set?mode="+s.mode;
  if(s.white)url+="&kelvin="+String(s.kelvin);
  else url+="&color="+s.hex;
  url+="&masterBrightness="+String(s.brightness)+"&animationStrength="+String(s.strength);
  url+="&strobeDelay="+bedroomFraction(s.strobeTenths);
  url+="&flashOffDelay="+bedroomFraction(s.flashTenths);
  url+="&chasePeriod="+String(s.chase)+"&wavePeriod="+String(s.wave);
  url+="&slowPulseCount="+String(s.slowPulseCount)+"&slowPulseMax="+String(s.slowPulseMax);
  url+="&rainbowPeriod="+String(s.rainbow)+"&candleHallFlickerAmount="+String(s.candleFlicker);
  return url;
}
bool bedroomWritePalette(const BedroomSnapshot& s){
  String p="/api/palettes/select?enabled="+String(s.paletteEnabled?1:0);
  if(s.paletteId.length())p+="&id="+s.paletteId;
  return bedroomHttpGet(p);
}
bool bedroomWriteTransitions(const BedroomSnapshot& s){
  return bedroomHttpGet("/api/transitions/set?enabled="+String(s.transitionsEnabled?1:0)+
    "&durationMs="+String(s.transitionDuration)+"&style=fade");
}
void bedroomError(const String& message){
  bedroom.result=message;bedroom.error=message;
  bedroom.stage=BED_FAILED;bedroom.due=0;
  logLine("BEDROOM RESTORE ATTENTION: "+message);
}
void bedroomCompleted(const String& message){
  bedroomDropRecovery();bedroom.count=0;bedroom.stage=BED_IDLE;
  bedroom.result=message;bedroom.error="";
  logLine("Bedroom: "+message);
}

bool bedroomQueue(const BedroomEffect& effect){
  if(!cfg.bedroomEnabled){bedroom.error="Bedroom strip disabled in System settings";return false;}
  if(bedroom.stage==BED_FAILED){bedroom.error="Unresolved strip restoration; recover or forget before another effect";return false;}
  if(effect.durationMs<200||effect.durationMs>600000||
     !bedroomValidMode(effect.mode)||!validColor(effect.color)||
     effect.brightness<0||effect.brightness>255||
     (effect.kelvin!=0&&(effect.kelvin<2700||effect.kelvin>6500))||
     effect.halfMs<100||effect.halfMs>5000)return false;
  if(bedroom.count>=BEDROOM_QUEUE_MAX){bedroom.error="Bedroom effect queue is full";return false;}
  bedroom.queue[bedroom.count++]=effect;
  bedroom.result="Queued "+effect.source;
  if(bedroom.stage==BED_IDLE){bedroom.stage=BED_CAPTURE;bedroom.due=millis();}
  return true;
}
bool bedroomQueueEvent(uint8_t i){
  if(i>=EVENT_COUNT)return false;
  const EventRule& r=cfg.events[i];
  if(!r.roomEnabled||!cfg.bedroomEnabled)return false;
  int brightness=nightAdjustedBrightness(r.brightness);
  if(brightness<=0)return false;
  BedroomEffect e;
  e.source=String("door-")+EVENT_NAMES[i];e.mode="strobe";e.color=r.color;
  e.brightness=brightness;e.halfMs=constrain((int)r.periodMs/2,100,5000);
  e.durationMs=r.durationMs;e.priority=i+1;
  return bedroomQueue(e);
}
// Only temporary actions are allowed to own the old strip. Permanent /set
// operations would wipe someone's ambient lighting without an undo boundary.
bool bedroomQueueAction(const DlAction& a,bool applyNight){
  if(!a.restore||a.durationSec<1||a.durationSec>600){
    bedroom.error="Bedroom shortcut requires Restore and duration 1..600 seconds";return false;
  }
  BedroomEffect e;e.source="shortcut";
  e.mode=a.stripMode.length()?a.stripMode:
    (a.operation=="pulse"?"strobe":"solid");
  e.color=a.color;e.brightness=bedroomPercentToByte(applyNight?dlEffectivePct(a.brightness):a.brightness);
  e.kelvin=a.kelvin;e.halfMs=a.stripHalfMs;
  e.durationMs=(uint32_t)a.durationSec*1000UL;e.priority=2;
  if(a.operation=="off")e.brightness=0;
  return bedroomQueue(e);
}

bool bedroomMatchesRequested(const BedroomSnapshot &s,const BedroomEffect& e){
  return s.mode==e.mode && s.brightness==e.brightness &&
    (e.kelvin>0?(s.white&&s.kelvin==e.kelvin):(!s.white&&s.hex==e.color)) &&
    (e.mode!="strobe"||abs(s.strobeTenths-(e.halfMs+50)/100)<=1);
}
bool bedroomLikelyPartialApply(const BedroomSnapshot& observed,const BedroomSnapshot& before,
                               const BedroomEffect& effect){
  // The pre-existing /set handler applies color/Kelvin before validating mode.
  // If an HTTP error leaves ONLY our requested color behind, roll it back.
  bool unchangedCore=observed.mode==before.mode &&
    observed.brightness==before.brightness &&
    observed.strength==before.strength &&
    observed.paletteId==before.paletteId && observed.paletteEnabled==before.paletteEnabled;
  bool ourColor=effect.kelvin>0?
    (observed.white&&observed.kelvin==effect.kelvin):
    (!observed.white&&observed.hex==effect.color);
  return unchangedCore && ourColor;
}
void bedroomStartNext(){
  if(bedroom.count==0){bedroom.stage=BED_PRE_RESTORE;bedroom.due=millis();return;}
  uint8_t index=0;
  for(uint8_t i=1;i<bedroom.count;i++)if(bedroom.queue[i].priority>bedroom.queue[index].priority)index=i;
  bedroom.current=bedroom.queue[index];
  for(uint8_t i=index;i+1<bedroom.count;i++)bedroom.queue[i]=bedroom.queue[i+1];
  bedroom.count--;
  // Respect existing Night Guard without changing its settings.
  if(bedroom.before.nightGuardEnabled&&bedroom.before.nightGuardBlockFlashing &&
    (bedroom.current.mode=="strobe"||bedroom.current.mode=="flash"||bedroom.current.mode=="stormOutside")){
    bedroom.current.mode="solid";
    bedroom.result="Night Guard blocked flashing: using solid alert";
  }
  bedroom.expectedValid=false;bedroom.applyAttempts=0;
  bedroom.stage=BED_APPLY;bedroom.due=millis();
}

void bedroomTick(){
  if(bedroom.stage==BED_IDLE||bedroom.stage==BED_FAILED)return;
  uint32_t now=millis();
  if((int32_t)(now-bedroom.due)<0)return;
  if(WiFi.status()!=WL_CONNECTED){bedroom.due=now+5000;return;}
  bedroom.error="";
  BedroomSnapshot observed;
  switch(bedroom.stage){
    case BED_CAPTURE:
      if(!bedroomRead(observed)){bedroom.due=now+BEDROOM_RETRY_PAUSE_MS;return;}
      if(observed.timerActive||observed.testActive||observed.transitionActive){
        // Timers/diagnostics/ongoing fades are not fully restorable via HTTP.
        bedroom.result="Skipped: strip timer, diagnostic or transition active";
        bedroom.count=0;bedroom.stage=BED_IDLE;return;
      }
      bedroom.before=observed;bedroom.snapshotPresent=true;
      bedroom.expectedValid=false;
      if(!bedroomStoreRecovery()){
        bedroomError("Could not persist original lighting snapshot in NVS");return;
      }
      bedroomStartNext();return;
    case BED_APPLY: {
      // Plan ownership in NVS *before* the remote request. On a reboot, an
      // unconfirmed effect is never blindly restored over a manual change.
      BedroomSnapshot planned=bedroom.before;
      planned.mode=bedroom.current.mode;planned.brightness=bedroom.current.brightness;
      planned.white=bedroom.current.kelvin>0;
      if(planned.white)planned.kelvin=bedroom.current.kelvin;
      else planned.hex=bedroom.current.color;
      if(planned.mode=="strobe")planned.strobeTenths=constrain((bedroom.current.halfMs+50)/100,1,50);
      bedroom.expected=planned;bedroom.expectedValid=true;
      if(bedroomFades.enabled){
        // Persist planned temporary transition settings BEFORE the remote write
        // so reboot recovery can still recognize our ownership.
        planned.transitionsEnabled=true;
        planned.transitionDuration=bedroomFades.fadeInMs;
        bedroom.expected=planned;
      }
      if(!bedroomStoreRecovery()){bedroomError("Could not persist planned effect");return;}
      if(bedroomFades.enabled &&
         !bedroomHttpGet("/api/transitions/set?enabled=1&durationMs="+
                         String(bedroomFades.fadeInMs)+"&style=fade")){
        bedroom.stage=BED_CHECK_APPLY;bedroom.due=millis()+250;
        bedroom.result="Fade preparation failed; verifying unchanged state before recovery";
        return;
      }
      String path="/set?mode="+bedroom.current.mode;
      path+=(bedroom.current.kelvin>0?"&kelvin="+String(bedroom.current.kelvin):"&color="+bedroom.current.color);
      path+="&masterBrightness="+String(bedroom.current.brightness);
      if(bedroom.current.mode=="strobe")path+="&strobeDelay="+bedroomFraction(planned.strobeTenths);
      // Start the timeout when the network command is attempted, not when
      // readback eventually returns. A Wi-Fi outage must not grant an alert
      // an extra several minutes of visible strobing after reconnect.
      bedroom.expires=millis()+bedroom.current.durationMs;
      bool wrote=bedroomHttpGet(path);
      bedroom.stage=BED_CHECK_APPLY;bedroom.due=millis()+250;
      if(!wrote)bedroom.result="Effect request uncertain; reading back before retry";
      return;
    }
    case BED_CHECK_APPLY:
      if(!bedroomRead(observed)){bedroom.due=millis()+BEDROOM_RETRY_PAUSE_MS;return;}
      if(bedroomMatchesRequested(observed,bedroom.current)){
        bedroom.expected=observed;bedroom.expectedValid=true;
        if(!bedroomStoreRecovery()){bedroomError("Could not persist observed effect");return;}
        bedroom.stage=(int32_t)(millis()-bedroom.expires)>=0?BED_PRE_RESTORE:BED_ACTIVE;
        bedroom.result=bedroom.stage==BED_ACTIVE?
          "Showing "+bedroom.current.source+"; original state saved":
          "Effect expired during network delay; starting restoration";
        bedroom.lastSource=bedroom.current.source;
      }else if(bedroomEqual(observed,bedroom.before,true)&&bedroom.applyAttempts<2){
        bedroom.applyAttempts++;bedroom.stage=BED_APPLY;
        bedroom.due=millis()+BEDROOM_RETRY_PAUSE_MS;return;
      }else if(bedroomLikelyPartialApply(observed,bedroom.before,bedroom.current) ||
               (bedroomFades.enabled&&bedroomEqual(observed,bedroom.before,false))){
        bedroom.expected=observed;bedroom.expectedValid=true;
        if(!bedroomStoreRecovery()){bedroomError("Could not persist partial-effect recovery");return;}
        bedroom.restoreAttempts=0;bedroom.stage=BED_RESTORE_FADE_SETUP;bedroom.due=millis();
        bedroom.result="Partially applied effect detected; rolling back immediately";return;
      }else{
        bedroomError("Effect not verified; original snapshot held for manual recovery");return;
      }
      bedroom.due=millis()+150;return;
    case BED_ACTIVE:
      if((int32_t)(now-bedroom.expires)<0){bedroom.due=min(bedroom.expires,now+400);return;}
      bedroom.stage=BED_PRE_RESTORE;bedroom.due=millis();return;
    case BED_PRE_RESTORE:
      if(!bedroomRead(observed)){bedroom.due=millis()+BEDROOM_RETRY_PAUSE_MS;return;}
      BedroomRestoreDecision decision=bedroomRestorationDecision(
        bedroomEqual(observed,bedroom.before,true),bedroom.expectedValid,
        bedroom.expectedValid&&bedroomEqual(observed,bedroom.expected,true),bedroom.forceRestore);
      if(decision==BEDROOM_ALREADY_RESTORED){
        bedroomCompleted("Already restored; verified original lighting state");return;
      }
      if(decision==BEDROOM_RESTORE_BLOCKED){
        bedroomError("Manual/unknown strip change detected; automatic restore stopped");return;
      }
      bedroom.forceRestore=false;
      if(bedroom.count>0){bedroomStartNext();return;}
      bedroom.restoreAttempts=0;bedroom.stage=BED_RESTORE_FADE_SETUP;bedroom.due=millis();return;
    case BED_RESTORE_FADE_SETUP:
      // Persist the expected temporary transition state before modifying it.
      // The previous user's transition settings remain in bedroom.before.
      if(bedroomFades.enabled){
        bedroom.expected.transitionsEnabled=true;
        bedroom.expected.transitionDuration=bedroomFades.fadeOutMs;
        if(!bedroomStoreRecovery()){
          bedroomError("Could not persist restore-fade ownership");return;
        }
        if(!bedroomHttpGet("/api/transitions/set?enabled=1&durationMs="+
                           String(bedroomFades.fadeOutMs)+"&style=fade")){
          bedroom.result="Restore fade setup failed, retrying";
          bedroom.due=millis()+BEDROOM_RETRY_PAUSE_MS;return;
        }
      }
      bedroom.stage=BED_RESTORE_BASE;bedroom.due=millis();return;
    case BED_RESTORE_BASE:
      bedroom.restoreAttempts++;
      if(!bedroomHttpGet(bedroomBasePath(bedroom.before))){
        bedroom.result="Restore command failed on attempt "+String(bedroom.restoreAttempts);
      }
      bedroom.stage=BED_RESTORE_PALETTE;bedroom.due=millis()+120;return;
    case BED_RESTORE_PALETTE:
      if(!bedroomWritePalette(bedroom.before))bedroom.result="Palette restore request failed";
      // Palette selection can restart the native transition; allow the whole
      // fade to complete before resetting the user's original preferences.
      bedroom.fadeEndAt=millis()+(bedroomFades.enabled?bedroomFades.fadeOutMs:0)+200;
      bedroom.stage=BED_RESTORE_TRANSITIONS;bedroom.due=bedroom.fadeEndAt;return;
    case BED_RESTORE_TRANSITIONS:
      // Never count a transition as complete while firmware still reports
      // transitionActive. A bounded fail-safe leads to a retry/failure state.
      if(bedroomFades.enabled){
        if(!bedroomRead(observed)){bedroom.due=millis()+BEDROOM_RETRY_PAUSE_MS;return;}
        if(observed.transitionActive && (uint32_t)(millis()-bedroom.fadeEndAt)<5000){
          bedroom.due=millis()+250;return;
        }
      }
      if(!bedroomWriteTransitions(bedroom.before))bedroom.result="Transition restore request failed";
      bedroom.stage=BED_VERIFY;bedroom.due=millis()+500;return;
    case BED_VERIFY:
      if(!bedroomRead(observed)){bedroom.due=millis()+BEDROOM_RETRY_PAUSE_MS;return;}
      if(bedroomEqual(observed,bedroom.before,true)){
        bedroomCompleted("Restored and verified; "+String(bedroom.restoreAttempts)+" attempt(s)");return;
      }
      if(bedroom.restoreAttempts<BEDROOM_MAX_RESTORE_ATTEMPTS){
        bedroom.result="Restore mismatch; retry "+String(bedroom.restoreAttempts+1)+"/3";
        bedroom.stage=BED_RESTORE_FADE_SETUP;bedroom.due=millis()+BEDROOM_RETRY_PAUSE_MS;
      }else bedroomError("Restore mismatch after 3 attempts; snapshot retained");
      return;
    case BED_IDLE:case BED_FAILED:return;
  }
}

bool bedroomSessionBusy(){return bedroom.stage!=BED_IDLE;}
void bedroomCancel(){
  bedroom.count=0;
  if(bedroom.stage==BED_IDLE)return;
  if(bedroom.stage==BED_CAPTURE){bedroom.stage=BED_IDLE;return;}
  if(bedroom.stage!=BED_FAILED){bedroom.stage=BED_PRE_RESTORE;bedroom.due=millis();}
}
void bedroomWriteStatus(JsonDocument& d){
  d["ok"]=true;d["stage"]=bedroomStageName(bedroom.stage);
  d["busy"]=bedroom.stage!=BED_IDLE;d["enabled"]=cfg.bedroomEnabled;
  d["host"]=cfg.bedroomHost;d["snapshotPresent"]=bedroom.snapshotPresent;
  d["readbackAvailable"]=bedroom.lastReadAt>0;
  d["observedAgeMs"]=bedroom.lastReadAt?millis()-bedroom.lastReadAt:0;
  d["observedMode"]=bedroom.observed.mode;d["observedColor"]=bedroom.observed.hex;
  d["observedBrightness"]=bedroom.observed.brightness;
  d["beforeMode"]=bedroom.snapshotPresent?bedroom.before.mode:"";
  d["queueDepth"]=bedroom.count;d["restoreAttempts"]=bedroom.restoreAttempts;
  d["fadeEnabled"]=bedroomFades.enabled;d["fadeInMs"]=bedroomFades.fadeInMs;
  d["fadeOutMs"]=bedroomFades.fadeOutMs;
  d["lastHttp"]=bedroom.lastHttp;d["result"]=bedroom.result;d["error"]=bedroom.error;
  d["warning"]="Exact animation frame, running timer and scene metadata cannot be restored over existing API";
}
void bedroomHandleStatus(){
  if(!authorized())return;
  DynamicJsonDocument d(1250);bedroomWriteStatus(d);jsonReply(200,d);
}
void bedroomHandleLive(){
  if(!authorized())return;
  BedroomSnapshot s;
  if(!bedroomRead(s)){reply(502,"Bedroom state unavailable: "+bedroom.error);return;}
  DynamicJsonDocument d(1150);d["ok"]=true;JsonObject state=d.createNestedObject("state");
  bedroomWriteSnapshot(state,s);
  d["timerActive"]=s.timerActive;d["transitionActive"]=s.transitionActive;d["testActive"]=s.testActive;
  jsonReply(200,d);
}
void bedroomHandleTest(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  BedroomEffect e;e.source="web-test";
  e.mode=String(d["mode"]|"strobe");e.color=bedroomHex(String(d["color"]|"FFB85B"));
  int pct=d["brightnessPct"]|25;
  int dur=d["durationSec"]|5;
  e.halfMs=d["halfMs"]|600;e.kelvin=d["kelvin"]|0;
  if(pct<0||pct>100||dur<1||dur>600||e.kelvin<0||(e.kelvin>0&&(e.kelvin<2700||e.kelvin>6500))){
    reply(400,"brightnessPct 0..100, durationSec 1..600, kelvin 0 or 2700..6500 required");return;
  }
  e.brightness=bedroomPercentToByte(dlEffectivePct(pct));
  e.durationMs=dur*1000UL;e.priority=3;
  if(!bedroomQueue(e)){reply(409,"Effect not queued: "+bedroom.error);return;}
  reply(202,"Temporary strip effect queued; see /api/bedroom/status for verification");
}
void bedroomHandleCancel(){
  if(!authorized())return;
  if(bedroom.stage==BED_IDLE){reply(200,"No bedroom effect active; no restore needed");return;}
  if(bedroom.stage==BED_FAILED){reply(409,"Recovery failed; use /api/bedroom/recover after inspecting live state");return;}
  bedroomCancel();reply(202,"Restore requested; check /api/bedroom/status");
}
void bedroomHandleRecover(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  if(!bedroom.snapshotPresent){reply(409,"No recovery snapshot exists");return;}
  bool force=d["force"]|false;
  bedroom.forceRestore=force;
  bedroom.stage=BED_PRE_RESTORE;bedroom.due=millis();
  reply(202,force?"Explicit force-restore requested":"Safe recovery queued; manual changes remain protected");
}
void bedroomHandleForget(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  if(String(d["confirm"]|"")!="FORGET"){
    reply(400,"confirm must be FORGET; this discards the saved restoration state");return;
  }
  bedroom.count=0;bedroomDropRecovery();bedroom.stage=BED_IDLE;
  bedroom.result="Recovery discarded on explicit user request";
  reply(200,"Saved recovery snapshot discarded; strip not changed");
}
void bedroomHandleFades(){
  if(!authorized())return;
  if(server.method()==HTTP_PUT){
    if(bedroomSessionBusy()){reply(409,"Wait for current strip restore before changing fade settings");return;}
    StaticJsonDocument<3072> d;if(!parseBody(d))return;
    int in=d["fadeInMs"]|bedroomFades.fadeInMs;
    int out=d["fadeOutMs"]|bedroomFades.fadeOutMs;
    if(!bedroomValidFade(in)||!bedroomValidFade(out)){
      reply(400,"Native strip fade must be 0, 500, 1500, 3000 or 5000 ms");return;
    }
    if(!d["enabled"].isNull()&&!d["enabled"].is<bool>()){
      reply(400,"enabled must be boolean");return;
    }
    bedroomFades.fadeInMs=in;bedroomFades.fadeOutMs=out;
    if(d["enabled"].is<bool>())bedroomFades.enabled=d["enabled"].as<bool>();
    bedroomSaveFades();
  }
  StaticJsonDocument<256> d;d["ok"]=true;d["enabled"]=bedroomFades.enabled;
  d["fadeInMs"]=bedroomFades.fadeInMs;d["fadeOutMs"]=bedroomFades.fadeOutMs;
  jsonReply(200,d);
}
void bedroomSetupRoutes(){
  server.on("/api/bedroom/fades",HTTP_GET,bedroomHandleFades);
  server.on("/api/bedroom/fades",HTTP_PUT,bedroomHandleFades);
  server.on("/api/bedroom/status",HTTP_GET,bedroomHandleStatus);
  server.on("/api/bedroom/live",HTTP_GET,bedroomHandleLive);
  server.on("/api/bedroom/test",HTTP_POST,bedroomHandleTest);
  server.on("/api/bedroom/cancel",HTTP_POST,bedroomHandleCancel);
  server.on("/api/bedroom/recover",HTTP_POST,bedroomHandleRecover);
  server.on("/api/bedroom/forget",HTTP_POST,bedroomHandleForget);
}
