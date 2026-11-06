#pragma once
/* Doorlight v4 extras: new bridge only. Neither original device needs a patch.
 * Matter bulbs are accessed through the existing Home Assistant REST API.
 * Fingerprint ID comes from the lock's EXISTING WebSocket logs and HTTP access log.
 */
struct R4Config {
  bool fingerprintEnabled=false;
  String ownFingerprints="";  // comma-separated IDs, explicitly supplied by owner
  String weatherEntity="weather.home";
  uint8_t gamingBrightness=18;
  String gamingColor="5366C8";
  bool gamingSuppressMotion=true;
  uint8_t workMinutes=25,breakMinutes=5,cycles=4;
  uint8_t workBrightness=65,breakBrightness=28;
} r4Cfg;

struct R4Fingerprint {
  bool awaitingNumber=false, baseline=false;
  size_t knownCount=0;
  int recentId=-1, entryId=-1;
  uint32_t scannedMs=0, entryMs=0, lastPoll=0, outsideMs=0;
  String entryClass="unknown", status="Not configured";
} r4Finger;

struct R4SavedBulb {
  bool captured=false;
  DlBulbState before;
  int expectedBrightness=-1;
  String expectedContext;
};
struct R4Session {
  bool active=false;
  R4SavedBulb bulbs[2];
} r4GameSnapshot,r4PomSnapshot;
struct R4Pomodoro {
  bool active=false, paused=false, breakStage=false;
  uint8_t cycle=1;
  uint32_t stageStart=0, pausedElapsed=0, lastVisualUpdate=0;
} r4Pom;

String r4WeatherStatus="Not checked";
String r4LastResult="No activity";
String r4GameSource="manual";
uint32_t r4GameLeaseLastMs=0;
uint32_t r4GameLeaseMs=90000;
bool r4GamePcLease=false;

bool r4FingerIsMine(int id){
  if(id<1||id>254||r4Cfg.ownFingerprints.length()==0)return false;
  String list=","+r4Cfg.ownFingerprints+",";
  return list.indexOf(","+String(id)+",")>=0;
}
bool r4ValidIdList(const String& v){
  if(v.length()>80)return false;
  if(v.isEmpty())return true;
  if(v.startsWith(",")||v.endsWith(",")||v.indexOf(",,")>=0)return false;
  int at=0;
  while(at<(int)v.length()){
    int next=v.indexOf(',',at);if(next<0)next=v.length();
    String id=v.substring(at,next);
    if(id.length()==0||id.length()>3)return false;
    for(size_t k=0;k<id.length();++k)if(!isdigit((unsigned char)id[k]))return false;
    int n=id.toInt();if(n<1||n>254)return false;
    at=next+1;
  }
  return true;
}
void r4OnLockLog(const String& s){
  if(!r4Cfg.fingerprintEnabled)return;
  if(s=="Fingerprint ID: "||s=="Fingerprint ID:"){
    r4Finger.awaitingNumber=true;return;
  }
  if(r4Finger.awaitingNumber){
    r4Finger.awaitingNumber=false;
    if(s.length()<1||s.length()>3)return;
    for(size_t k=0;k<s.length();++k)if(!isdigit((unsigned char)s[k]))return;
    int id=s.toInt();
    if(id>=1&&id<=254){r4Finger.recentId=id;r4Finger.scannedMs=millis();r4Finger.status="Received wireless fingerprint ID";}
  }
}
// Existing lock's GET /getAccessLogs returns an array of {fingerprintID,timestamp}.
// Its fixed session cookie is checked in the original code. Polling is bounded.
bool r4PollAccessLogs(){
  if(!r4Cfg.fingerprintEnabled||WiFi.status()!=WL_CONNECTED)return false;
  uint32_t now=millis();if(r4Finger.lastPoll!=0 && (uint32_t)(now-r4Finger.lastPoll)<1800)return false;
  r4Finger.lastPoll=now;
  WiFiClient client;HTTPClient http;
  http.setConnectTimeout(600);http.setTimeout(1200);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if(!http.begin(client,"http://"+cfg.lockHost+"/getAccessLogs"))return false;
  http.addHeader("Cookie","ESPSESSIONID=1");
  int code=http.GET();
  int len=http.getSize();
  if(code!=200||len>16384||len==0){http.end();r4Finger.status="Access-log request unavailable";return false;}
  String body=http.getString();http.end();
  if(body.length()>16384)return false;
  DynamicJsonDocument doc(18000);
  if(deserializeJson(doc,body)||!doc.is<JsonArray>()){r4Finger.status="Invalid access-log response";return false;}
  JsonArrayConst items=doc.as<JsonArrayConst>();size_t count=items.size();
  if(!r4Finger.baseline){r4Finger.baseline=true;r4Finger.knownCount=count;r4Finger.status="Access-log baseline established";return true;}
  if(count<r4Finger.knownCount){r4Finger.knownCount=count;r4Finger.recentId=-1;return false;}
  // Only accept exactly one new, recognized ID. Multiple new scans are ambiguous.
  if(count==r4Finger.knownCount+1){
    int id=items[count-1]["fingerprintID"]|-1;
    if(id>=1&&id<=254){r4Finger.recentId=id;r4Finger.scannedMs=now;r4Finger.status="New wireless access-log ID";}
  }else if(count>r4Finger.knownCount+1){r4Finger.recentId=-1;r4Finger.status="Multiple scans: identity ambiguous";}
  r4Finger.knownCount=count;
  return true;
}
void r4OnOutsideMotion(){
  if(!r4Cfg.fingerprintEnabled)return;
  r4Finger.outsideMs=millis();
  // Establish a log baseline before someone reaches the fingerprint scanner.
  if(!r4Finger.baseline)r4PollAccessLogs();
}
void r4OnEntry(){
  r4Finger.entryClass="unknown";r4Finger.entryId=-1;
  r4Finger.entryMs=millis();
  if(!r4Cfg.fingerprintEnabled||r4Cfg.ownFingerprints.length()==0)return;
  r4PollAccessLogs();
  const uint32_t age=(uint32_t)(millis()-r4Finger.scannedMs);
  if(r4Finger.recentId>0 && r4Finger.scannedMs!=0 && age<=15000){
    r4Finger.entryId=r4Finger.recentId;
    r4Finger.entryClass=r4FingerIsMine(r4Finger.entryId)?"me":"other";
    r4Finger.recentId=-1;  // One scan cannot identify multiple entries.
    logLine("Entry correlated with fingerprint ID "+String(r4Finger.entryId)+" (not proof of person)");
  }else r4Finger.status="Entry with no recent confirmed fingerprint; identity unknown";
}
bool r4FingerprintCondition(bool own){
  if(!r4Cfg.fingerprintEnabled || r4Finger.entryId<1 ||
     (uint32_t)(millis()-r4Finger.entryMs)>7000)return false;
  return r4Finger.entryClass==(own?"me":"other");
}
void r4WeatherPlan(int& kelvin,String& color){
  kelvin=4200;color="";
  if(!dlHAReady()||!dlValidEntity(r4Cfg.weatherEntity,"weather")){
    r4WeatherStatus="Weather unavailable: warm sunrise fallback";return;
  }
  DynamicJsonDocument d(2048);
  if(!dlReadEntity(r4Cfg.weatherEntity,d)){
    r4WeatherStatus="Weather offline: warm sunrise fallback";return;
  }
  String state=d["state"]|"unknown";
  r4WeatherStatus=state;
  if(state=="sunny"||state=="clear-night")kelvin=3300;
  else if(state=="rainy"||state=="pouring"||state=="lightning-rainy")kelvin=2800;
  else if(state=="snowy"||state=="snowy-rainy")kelvin=5000;
  else if(state=="cloudy"||state=="partlycloudy"||state=="fog")kelvin=3800;
}
// Store session snapshots in Home Assistant scenes, not the existing LED firmware.
String r4SceneId(const String& kind,uint8_t i){return "doorlight_"+kind+"_"+String(i+1);}
bool r4Scene(const String& kind,uint8_t i,const String& action){
  DynamicJsonDocument d(256);
  if(action=="create"){
    d["scene_id"]=r4SceneId(kind,i);
    d.createNestedArray("snapshot_entities").add(dlBulbConfig.entities[i]);
  }else {
    d["entity_id"]="scene."+r4SceneId(kind,i);
    if(action=="turn_on")d["transition"]=3; // Best-effort HA scene fade
  }
  String body;serializeJson(d,body);String response;int code;
  return dlHARequest("/api/services/scene/"+action,"POST",body,response,code);
}
bool r4Capture(R4Session& session,const String& kind){
  if(!dlHAReady()||session.active)return false;
  for(int i=0;i<2;i++){
    if(dlTemporary[i].active){r4LastResult="Temporary bulb alert is running";return false;}
  }
  for(int i=0;i<2;i++){
    R4SavedBulb &b=session.bulbs[i];b=R4SavedBulb();
    if(!dlReadBulb(i,b.before) || !r4Scene(kind,i,"create")){
      for(int j=0;j<=i;j++)if(session.bulbs[j].captured)r4Scene(kind,j,"delete");
      r4LastResult="Could not snapshot smart bulbs";return false;
    }
    b.captured=true;
  }
  session.active=true;return true;
}
void r4MarkExpected(R4Session& session,uint8_t i,int pct){
  session.bulbs[i].expectedBrightness=(pct*255+50)/100;
  session.bulbs[i].expectedContext=dlTemporary[i].expectedContextId;
}
void r4SessionLighting(R4Session& session,int pct,const String& color,int kelvin,int fade){
  for(int i=0;i<2;i++)if(session.bulbs[i].captured){
    dlTemporary[i].expectedContextId="";
    if(dlSendService(i,"on",pct,color,kelvin,fade))r4MarkExpected(session,i,pct);
    else r4LastResult="Bulb command failed during session";
  }
}
void r4Restore(R4Session& session,const String& kind){
  if(!session.active)return;
  for(int i=0;i<2;i++)if(session.bulbs[i].captured){
    R4SavedBulb &b=session.bulbs[i];DlBulbState current;
    bool good=dlReadBulb(i,current);
    bool ours=good&&current.on&&b.expectedBrightness>=0&&
       abs(current.brightness-b.expectedBrightness)<=14;
    if(good&&!b.expectedContext.isEmpty()&&!current.contextId.isEmpty())
      ours=current.contextId==b.expectedContext;
    if(ours){
      if(!r4Scene(kind,i,"turn_on"))r4LastResult="Restoration failed for bulb "+String(i+1);
    }else r4LastResult="Skipped restore for bulb "+String(i+1)+" (manual change/offline)";
    r4Scene(kind,i,"delete");b=R4SavedBulb();
  }
  session.active=false;
}
void r4StartGaming(){
  if(r4Pom.active){r4LastResult="Stop Pomodoro before Gaming Mode";return;}
  if(!r4Capture(r4GameSnapshot,"game"))return;
  r4SessionLighting(r4GameSnapshot,dlEffectivePct(r4Cfg.gamingBrightness),r4Cfg.gamingColor,0,3);
  r4LastResult="Gaming Mode enabled";dlAutomationTrigger("gaming_start");
}
void r4StopGaming(){
  if(!r4GameSnapshot.active)return;
  r4Restore(r4GameSnapshot,"game");dlAutomationTrigger("gaming_end");
  r4LastResult="Gaming Mode ended";
}
bool r4GamingSuppresses(const String& type){
  return r4GameSnapshot.active&&r4Cfg.gamingSuppressMotion&&
    (type=="motion"||type=="walkby"||type=="loiter");
}
uint32_t r4StageDuration(){return (uint32_t)(r4Pom.breakStage?r4Cfg.breakMinutes:r4Cfg.workMinutes)*60000UL;}
void r4PomStageLighting(){
  if(!r4PomSnapshot.active)return;
  int pct=dlEffectivePct(r4Pom.breakStage?r4Cfg.breakBrightness:r4Cfg.workBrightness);
  r4SessionLighting(r4PomSnapshot,pct,r4Pom.breakStage?"FFC780":"B9DFFF",0,4);
}
void r4PomStart(){
  if(r4GameSnapshot.active){r4LastResult="Stop Gaming Mode before Pomodoro";return;}
  if(!r4Capture(r4PomSnapshot,"pom"))return;
  r4Pom=R4Pomodoro();r4Pom.active=true;r4Pom.stageStart=millis();
  r4PomStageLighting();dlAutomationTrigger("pomodoro_work");r4LastResult="Pomodoro started";
}
void r4PomStop(){
  if(!r4Pom.active)return;
  r4Pom=R4Pomodoro();r4Restore(r4PomSnapshot,"pom");
  dlAutomationTrigger("pomodoro_done");r4LastResult="Pomodoro stopped";
}
void r4PomTick(){
  if(!r4Pom.active||r4Pom.paused)return;
  uint32_t now=millis();uint32_t elapsed=now-r4Pom.stageStart;
  if(elapsed>=r4StageDuration()){
    if(r4Pom.breakStage){
      if(r4Pom.cycle>=r4Cfg.cycles){r4PomStop();return;}
      r4Pom.cycle++;r4Pom.breakStage=false;
    }else r4Pom.breakStage=true;
    r4Pom.stageStart=now;r4Pom.lastVisualUpdate=now;
    r4PomStageLighting();dlAutomationTrigger(r4Pom.breakStage?"pomodoro_break":"pomodoro_work");return;
  }
  if(!r4Pom.breakStage&&(uint32_t)(now-r4Pom.lastVisualUpdate)>=60000){
    r4Pom.lastVisualUpdate=now;
    // A subtle minute-by-minute progress indicator: gradual brightness reduction.
    int endPct=max(1,(int)r4Cfg.workBrightness/3);
    int pct=(int)r4Cfg.workBrightness-(int)((uint32_t)(r4Cfg.workBrightness-endPct)*elapsed/r4StageDuration());
    r4SessionLighting(r4PomSnapshot,dlEffectivePct(pct),"B9DFFF",0,50);
  }
}
void r4Save(){
  StaticJsonDocument<600>d;
  d["fingerprintEnabled"]=r4Cfg.fingerprintEnabled;
  d["ownFingerprints"]=r4Cfg.ownFingerprints;d["weatherEntity"]=r4Cfg.weatherEntity;
  d["gamingBrightness"]=r4Cfg.gamingBrightness;d["gamingColor"]=r4Cfg.gamingColor;
  d["gamingSuppressMotion"]=r4Cfg.gamingSuppressMotion;
  d["workMinutes"]=r4Cfg.workMinutes;d["breakMinutes"]=r4Cfg.breakMinutes;
  d["cycles"]=r4Cfg.cycles;d["workBrightness"]=r4Cfg.workBrightness;d["breakBrightness"]=r4Cfg.breakBrightness;
  String s;serializeJson(d,s);prefs.putString("r4",s);
}
void r4Load(){
  String s=prefs.getString("r4","");if(s.isEmpty())return;
  StaticJsonDocument<600>d;if(deserializeJson(d,s))return;
  String ids=d["ownFingerprints"]|"";if(r4ValidIdList(ids))r4Cfg.ownFingerprints=ids;
  r4Cfg.fingerprintEnabled=d["fingerprintEnabled"]|false;
  String weather=d["weatherEntity"]|"";if(dlValidEntity(weather,"weather"))r4Cfg.weatherEntity=weather;
  String color=d["gamingColor"]|"";if(validColor(color))r4Cfg.gamingColor=color;
  int n=d["gamingBrightness"]|18;if(n>=1&&n<=100)r4Cfg.gamingBrightness=n;
  r4Cfg.gamingSuppressMotion=d["gamingSuppressMotion"]|true;
  n=d["workMinutes"]|25;if(n>=1&&n<=120)r4Cfg.workMinutes=n;
  n=d["breakMinutes"]|5;if(n>=1&&n<=60)r4Cfg.breakMinutes=n;
  n=d["cycles"]|4;if(n>=1&&n<=12)r4Cfg.cycles=n;
  n=d["workBrightness"]|65;if(n>=1&&n<=100)r4Cfg.workBrightness=n;
  n=d["breakBrightness"]|28;if(n>=1&&n<=100)r4Cfg.breakBrightness=n;
}
void r4WriteStatus(JsonDocument& d){
  d["ok"]=true;
  JsonObject c=d.createNestedObject("settings");
  c["fingerprintEnabled"]=r4Cfg.fingerprintEnabled;
  c["ownFingerprints"]=r4Cfg.ownFingerprints;c["weatherEntity"]=r4Cfg.weatherEntity;
  c["gamingBrightness"]=r4Cfg.gamingBrightness;c["gamingColor"]=r4Cfg.gamingColor;
  c["gamingSuppressMotion"]=r4Cfg.gamingSuppressMotion;
  c["workMinutes"]=r4Cfg.workMinutes;c["breakMinutes"]=r4Cfg.breakMinutes;
  c["cycles"]=r4Cfg.cycles;c["workBrightness"]=r4Cfg.workBrightness;
  c["breakBrightness"]=r4Cfg.breakBrightness;
  d["gamingActive"]=r4GameSnapshot.active;
  d["gamingSource"]=r4GameSnapshot.active?r4GameSource:"none";
  d["gamingLeaseRemainingSec"]=r4GamePcLease&&r4GameSnapshot.active?
    max(0,(int)((r4GameLeaseMs-min((uint32_t)(millis()-r4GameLeaseLastMs),r4GameLeaseMs))/1000)):0;
  d["pomodoroActive"]=r4Pom.active;d["pomodoroPaused"]=r4Pom.paused;
  d["pomodoroCycle"]=r4Pom.cycle;d["pomodoroStage"]=r4Pom.breakStage?"break":"work";
  uint32_t elapsed=r4Pom.paused?r4Pom.pausedElapsed:millis()-r4Pom.stageStart;
  d["pomodoroRemainingSec"]=r4Pom.active?max(0,(int)(r4StageDuration()-min(elapsed,r4StageDuration()))/1000):0;
  d["fingerprintStatus"]=r4Finger.status;d["fingerprintEntryClass"]=r4Finger.entryClass;
  d["lastFingerprintId"]=r4Finger.entryId;
  d["weatherStatus"]=r4WeatherStatus;d["lastResult"]=r4LastResult;
}
void r4HandleStatus(){if(!authorized())return;DynamicJsonDocument d(1700);r4WriteStatus(d);jsonReply(200,d);}
void r4HandleSettings(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  R4Config next=r4Cfg;
  next.fingerprintEnabled=d["fingerprintEnabled"]|next.fingerprintEnabled;
  next.ownFingerprints=String(d["ownFingerprints"]|next.ownFingerprints);
  next.weatherEntity=String(d["weatherEntity"]|next.weatherEntity);
  next.gamingBrightness=d["gamingBrightness"]|next.gamingBrightness;
  next.gamingColor=String(d["gamingColor"]|next.gamingColor);next.gamingColor.toUpperCase();
  next.gamingSuppressMotion=d["gamingSuppressMotion"]|next.gamingSuppressMotion;
  next.workMinutes=d["workMinutes"]|next.workMinutes;
  next.breakMinutes=d["breakMinutes"]|next.breakMinutes;
  next.cycles=d["cycles"]|next.cycles;
  next.workBrightness=d["workBrightness"]|next.workBrightness;
  next.breakBrightness=d["breakBrightness"]|next.breakBrightness;
  if(!r4ValidIdList(next.ownFingerprints)||!dlValidEntity(next.weatherEntity,"weather")||
     !validColor(next.gamingColor)||next.gamingBrightness<1||next.gamingBrightness>100||
     next.workMinutes<1||next.workMinutes>120||next.breakMinutes<1||next.breakMinutes>60||
     next.cycles<1||next.cycles>12||next.workBrightness<1||next.workBrightness>100||
     next.breakBrightness<1||next.breakBrightness>100){reply(400,"Invalid routine settings");return;}
  if(next.fingerprintEnabled && next.ownFingerprints.isEmpty()){
    reply(400,"Specify your fingerprint IDs before enabling identity-aware rules");return;
  }
  r4Cfg=next;r4Save();reply(200,"Advanced routine settings saved");
}
void r4HandleGaming(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  if(!d["active"].is<bool>()){reply(400,"Set active to true/false");return;}
  bool desired=d["active"].as<bool>();
  String source=d["source"]|"manual";
  if(source!="manual" && source!="pc-watcher"){
    reply(400,"source must be manual or pc-watcher");return;
  }
  if(r4GameSnapshot.active && r4GameSource!=source){
    reply(409,"Gaming Mode already controlled by "+r4GameSource);return;
  }
  if(desired && !r4GameSnapshot.active){
    r4StartGaming();
    if(r4GameSnapshot.active)r4GameSource=source;
  }else if(!desired && r4GameSnapshot.active){
    r4StopGaming();r4GamePcLease=false;r4GameSource="manual";
  }
  if(desired!=r4GameSnapshot.active){reply(409,r4LastResult);return;}
  if(desired && source=="pc-watcher"){
    int seconds=d["leaseSeconds"]|90;
    if(seconds<30||seconds>600){reply(400,"leaseSeconds must be 30..600");return;}
    r4GamePcLease=true;r4GameLeaseMs=(uint32_t)seconds*1000UL;
    r4GameLeaseLastMs=millis();
  }else if(desired){r4GamePcLease=false;}
  reply(200,r4LastResult);
}
void r4HandlePomodoro(){
  if(!authorized())return;StaticJsonDocument<3072>d;if(!parseBody(d))return;
  String action=d["action"]|"";
  if(action=="start"){r4PomStart();if(!r4Pom.active){reply(409,r4LastResult);return;}}
  else if(action=="stop")r4PomStop();
  else if(action=="pause"&&r4Pom.active&&!r4Pom.paused){
    r4Pom.pausedElapsed=millis()-r4Pom.stageStart;r4Pom.paused=true;
  }else if(action=="resume"&&r4Pom.active&&r4Pom.paused){
    r4Pom.stageStart=millis()-r4Pom.pausedElapsed;r4Pom.paused=false;
  }else{reply(400,"Use start, stop, pause, or resume");return;}
  reply(200,r4LastResult);
}
void r4SetupRoutes(){
  server.on("/api/routines",HTTP_GET,r4HandleStatus);
  server.on("/api/routines",HTTP_PUT,r4HandleSettings);
  server.on("/api/gaming",HTTP_POST,r4HandleGaming);
  server.on("/api/pomodoro",HTTP_POST,r4HandlePomodoro);
}
void r4Tick(){
  r4PomTick();
  if(r4GamePcLease&&r4GameSnapshot.active &&
     (uint32_t)(millis()-r4GameLeaseLastMs)>r4GameLeaseMs){
    logLine("PC Gaming Mode lease expired; restoring saved bulb scene");
    r4StopGaming();r4GamePcLease=false;r4GameSource="manual";
  }
}
