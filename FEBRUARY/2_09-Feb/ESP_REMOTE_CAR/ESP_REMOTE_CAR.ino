#include <WiFi.h>
#include <WebServer.h>

// ===== LCD 16x2 I2C =====
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= WiFi =================
const char* apSsid = "ESP_CAR";        // offline mode
const char* apPassword = "326681285577";

const char* staSsid = "Airtel_ARV15"; // My Wi-Fi address
const char* staPassword = "Arvind@99";

WebServer server(80);


// ================= L298N =================
#define IN1 14
#define IN2 27
#define IN3 26
#define IN4 25
#define ENA_PIN 33
#define ENB_PIN 32

// ================= PWM =================
const int PWM_FREQ = 20000;
const int PWM_RES  = 8;
const int CH_A = 0;
const int CH_B = 1;

// ================= Speed (ONE SPEED FOR ALL MODES) =================
uint8_t carSpeed = 160;            // slider controls this
const uint8_t MIN_SPEED = 160;     // car starts moving from 160
const uint8_t MAX_SPEED = 255;

// ================= Ultrasonic =================
#define TRIG_PIN 18
#define ECHO_PIN 19

// ===== Ultrasonic NON-BLOCKING CACHE =====
float cachedDistance = -1;
unsigned long lastUltrasonicRead = 0;
const unsigned long ULTRA_INTERVAL = 70; // ms


// ================= LED PINS =================
#define LED_LEFT   16   // left indicator
#define LED_RIGHT  17   // right indicator
#define LED_HEAD   23   // headlight
#define LED_BRAKE  13   // brake light

// ================= Flags =================
bool forwardCmd = false;
bool backCmd = false;
bool leftCmd = false;
bool rightCmd = false;

bool obstacleMode = false;
bool followMode = false;
bool voiceMode = false; // Add this with the other flags

// ================= Auto Obstacle =================
enum AutoState { AUTO_IDLE,
                 AUTO_FORWARD,
                 AUTO_BACKWARD,
                 AUTO_TURN };
AutoState autoState = AUTO_IDLE;

unsigned long autoStateStart = 0;
bool turnLeftNext = true;

const unsigned long BACK_TIME = 600;
const unsigned long TURN_TIME = 700;
const float OBSTACLE_DIST_CM = 20.0;

// ================ LCD UPDATE TIMER =================
unsigned long lastLcdUpdate = 0;
const unsigned long LCD_INTERVAL = 300; // ms

// ================ Connection Status (Web Client) =================
unsigned long lastClientSeen = 0;
bool clientConnected = false;
bool prevClientConnected = false;

// since we use ping every 1 sec
const unsigned long DISCONNECT_TIMEOUT = 3000; // 3 seconds

unsigned long statusMsgStart = 0;
String statusMsg = "";
const unsigned long STATUS_MSG_TIME = 1200; // ms

// ================= Indicator Control =================
// Manual toggles
bool headlightOn = false;
bool manualLeftInd = false;
bool manualRightInd = false;
bool hazardOn = false;

// Blinking timer
unsigned long lastBlink = 0;
const unsigned long BLINK_INTERVAL = 350; // ms
bool blinkState = false;

// In auto mode, show turning indicator
enum IndMode { IND_OFF, IND_LEFT, IND_RIGHT, IND_HAZARD };
IndMode activeIndicator = IND_OFF;

//Brake light memory (ON only when STOP button pressed)
bool brakeByStopButton = false;

// ================= HTML =================
const char index_html[] PROGMEM = R"=====(<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Robot Car</title>
<style>
body{ margin:0; font-family:Arial, sans-serif; background:#111; color:#fff; overflow:hidden; }
#rotateNotice{ display:none; position:fixed; inset:0; background:#000; color:#fff; align-items:center; justify-content:center; text-align:center; font-size:20px; z-index:999; }
#app{ display:block; }
@media (orientation: portrait){ #app{ display:none; } #rotateNotice{ display:flex; } }

/* NAVBAR */
.navbar{ display:flex; justify-content:space-between; align-items:center; padding:10px; background:#1e1e1e; border-bottom: 2px solid #333; }
.nav-group{ display:flex; gap:8px; align-items:center; }
.nav-btn{ padding:10px 14px; border:none; border-radius:8px; background:#444; color:#fff; font-size:12px; cursor:pointer; transition: 0.2s; font-weight:bold; }
.nav-btn.on{ background:#f1c40f; color:#000; }

/* NAVBAR MODE BADGE */
#modeBadge { font-size: 11px; background: #000; color: #888; padding: 5px 12px; border-radius: 15px; border: 1px solid #888; min-width: 90px; text-align: center; font-weight: bold; text-transform: uppercase; transition: 0.3s; }

/* ANIMATIONS */
@keyframes blink { 0% { background: #f1c40f; color: #000; } 50% { background: #444; color: #fff; } 100% { background: #f1c40f; color: #000; } }
.blink { animation: blink 0.8s infinite; }

@keyframes micPulse { 0% { box-shadow: 0 0 0 0 rgba(46, 204, 113, 0.7); background: #2ecc71; } 70% { box-shadow: 0 0 0 15px rgba(46, 204, 113, 0); background: #27ae60; } 100% { box-shadow: 0 0 0 0 rgba(46, 204, 113, 0); background: #2ecc71; } }
.mic-listening { animation: micPulse 1.5s infinite !important; color: #000 !important; }

/* BUTTONS */
.btn{ padding:16px 22px; margin:8px; border:none; border-radius:10px; background:#444; color:#fff; font-size:18px; cursor:pointer; user-select: none; transition: 0.2s; }
.btn:active:not(.manual-disabled) { background:#2ecc71 !important; color:#000 !important; }
.btn.green{ background:#2ecc71 !important; color:#000 !important; }
.btn.red{ background:#e74c3c !important; color:#fff !important; }
.btn.grey{ background:#444 !important; color:#fff !important; }

/* DISABLE STATES */
.manual-disabled{ background:#222 !important; color:#555 !important; opacity:0.3; cursor: not-allowed; pointer-events: none !important; }
.mic-disabled{ opacity: 0.4; cursor: not-allowed; pointer-events: none; }

.container{ padding:12px; text-align:center; }
.select-box{ width:70%; max-width:320px; padding:12px; font-size:16px; border-radius:8px; border:2px solid #555; background:#444; color:#fff; outline: none; }

.panel{ display:none; padding:10px; text-align:center; }
.manual-wrapper{ display:flex; height:calc(100vh - 160px); }
.manual-left, .manual-right{ flex:1; display:flex; align-items:center; justify-content:center; }
.manual-left{ flex-direction:column; justify-content:space-between; padding:20px 0; }
.manual-left .btn, .manual-right .btn{ width:160px; }
#voiceStat{ color: #2ecc71; margin-top: 10px; font-weight: bold; height: 20px; }
</style>
</head>
<body>

<div id="rotateNotice"><div>🔄 Rotate your phone<br>Landscape mode required</div></div>

<div id="app">
<div class="navbar">
  <div class="nav-group"><button id="leftIndBtn" class="nav-btn" onclick="toggleLeftInd()">LEFT IND</button></div>
  <div class="nav-group">
    <button id="headBtn" class="nav-btn" onclick="toggleHead()">HEADLIGHT</button>
    <div id="modeBadge">MANUAL</div>
    <button id="hazBtn" class="nav-btn" onclick="toggleHaz()">HAZARD</button>
  </div>
  <div class="nav-group"><button id="rightIndBtn" class="nav-btn" onclick="toggleRightInd()">RIGHT IND</button></div>
</div>

<div class="container">
  <select id="modeSelect" class="select-box" onchange="changeMode(this.value)">
    <option value="manual">Manual Mode</option>
    <option value="auto">Automatic Mode</option>
    <option value="follow">Follow Mode</option>
    <option value="voice">Voice Mode</option>
  </select>
</div>

<div id="manualPanel" class="panel">
  <div class="manual-wrapper">
    <div class="manual-left">
      <button class="btn manual-btn" onmousedown="move('f')" ontouchstart="move('f')">FORWARD</button>
      <button id="stopBtn" class="btn manual-btn" onclick="stopCar()">STOP</button>
      <button class="btn manual-btn" onmousedown="move('b')" ontouchstart="move('b')">BACKWARD</button>
    </div>
    <div class="manual-right">
      <button class="btn manual-btn" onmousedown="move('l')" ontouchstart="move('l')">LEFT</button>
      <button class="btn manual-btn" onmousedown="move('r')" ontouchstart="move('r')">RIGHT</button>
    </div>
  </div>
</div>

<div id="autoPanel" class="panel">
  <button id="autoOnBtn" class="btn" onclick="setAuto(true)">ON</button>
  <button id="autoOffBtn" class="btn red" onclick="setAuto(false)">OFF</button>
</div>

<div id="followPanel" class="panel">
  <button id="followOnBtn" class="btn" onclick="setFollow(true)">ON</button>
  <button id="followOffBtn" class="btn red" onclick="setFollow(false)">OFF</button>
</div>

<div id="voicePanel" class="panel">
  <button id="voiceOnBtn" class="btn" onclick="setVoice(true)">ON</button>
  <button id="voiceOffBtn" class="btn red" onclick="setVoice(false)">OFF</button>
  <br><br>
  <button id="micBtn" class="btn mic-disabled" onclick="startMic()">TAP TO SPEAK</button>
  <div id="voiceStat">Ready</div>
</div>
</div>

<script>
let isManualLocked = false;
let isVoiceActive=false, isAutoActive=false, isFollowActive=false;
let voiceTimeout, countdownInterval;

function cmd(d,s){ fetch(`/cmd?dir=${d}&state=${s}`); }
function hideAll(){ document.querySelectorAll('.panel').forEach(p=>p.style.display='none'); }

/* THE GLOBAL BADGE REFRESHER */
function refreshGlobalBadge() {
  const badge = document.getElementById("modeBadge");
  if(isAutoActive) { badge.innerText = "AUTO ON"; badge.style.color = badge.style.borderColor = "#2ecc71"; }
  else if(isFollowActive) { badge.innerText = "FOLLOW ON"; badge.style.color = badge.style.borderColor = "#2ecc71"; }
  else if(isVoiceActive) { badge.innerText = "VOICE ON"; badge.style.color = badge.style.borderColor = "#2ecc71"; }
  else { badge.innerText = "MANUAL"; badge.style.color = badge.style.borderColor = "#888"; }
}

function changeMode(m){
  hideAll();
  const panel = document.getElementById(m + "Panel");
  if(panel) panel.style.display = "block";
}

function stopCar(){ if(isManualLocked) return; document.getElementById("stopBtn").classList.add("red"); cmd('s',1); }
function move(d){ if(isManualLocked) return; document.getElementById("stopBtn").classList.remove("red"); cmd(d,1); setTimeout(()=>cmd(d,0), 250); }

function updateManualState() {
  isManualLocked = (isAutoActive || isFollowActive || isVoiceActive);
  document.querySelectorAll('.manual-btn').forEach(btn => btn.classList.toggle('manual-disabled', isManualLocked));
}

/* RESET ALL MODES EXCEPT THE NEW ONE */
function resetAllModes(except) {
  if(except !== 'auto' && isAutoActive) setAuto(false);
  if(except !== 'follow' && isFollowActive) setFollow(false);
  if(except !== 'voice' && isVoiceActive) setVoice(false);
}

/* MODE TOGGLES */
function setAuto(v){
  if(v) resetAllModes('auto'); // Turn off others if turning this ON
  isAutoActive = v;
  document.getElementById("autoOnBtn").className=v?"btn green":"btn grey";
  document.getElementById("autoOffBtn").className=v?"btn grey":"btn red";
  refreshGlobalBadge(); updateManualState(); fetch(`/mode?auto=${v?1:0}`);
}

function setFollow(v){
  if(v) resetAllModes('follow');
  isFollowActive = v;
  document.getElementById("followOnBtn").className=v?"btn green":"btn grey";
  document.getElementById("followOffBtn").className=v?"btn grey":"btn red";
  refreshGlobalBadge(); updateManualState(); fetch(`/follow?f=${v?1:0}`);
}

function setVoice(v){
  if(v) resetAllModes('voice');
  isVoiceActive = v;
  document.getElementById("voiceOnBtn").className=v?"btn green":"btn grey";
  document.getElementById("voiceOffBtn").className=v?"btn grey":"btn red";
  document.getElementById("micBtn").classList.toggle("mic-disabled", !v);
  refreshGlobalBadge(); updateManualState(); fetch(`/voice?v=${v?1:0}`);
}

/* INDICATORS & HAZARD */
let lOn=false, rOn=false, hOn=false, headOn=false;
function toggleHead(){ headOn=!headOn; document.getElementById("headBtn").classList.toggle("on",headOn); fetch(`/head?on=${headOn?1:0}`); }
function toggleLeftInd(){ if(hOn) return; lOn=!lOn; rOn=false; updateIndicatorUI(); fetch(`/ind?side=L&on=${lOn?1:0}`); }
function toggleRightInd(){ if(hOn) return; rOn=!rOn; lOn=false; updateIndicatorUI(); fetch(`/ind?side=R&on=${rOn?1:0}`); }
function toggleHaz(){ hOn=!hOn; if(hOn){ lOn=false; rOn=false; } updateIndicatorUI(); fetch(`/haz?on=${hOn?1:0}`); }
function updateIndicatorUI(){
  const lB=document.getElementById("leftIndBtn"), rB=document.getElementById("rightIndBtn"), hB=document.getElementById("hazBtn");
  lB.classList.remove("blink"); rB.classList.remove("blink"); hB.classList.remove("on");
  if(hOn){ hB.classList.add("on"); lB.classList.add("blink"); rB.classList.add("blink"); }
  else { if(lOn) lB.classList.add("blink"); if(rOn) rB.classList.add("blink"); }
}

/* VOICE LOGIC */
let recognition;
if('webkitSpeechRecognition' in window){
  recognition = new webkitSpeechRecognition();
  recognition.onresult = (e) => {
    const text = e.results[0][0].transcript.toLowerCase();
    document.getElementById("voiceStat").innerText = "Heard: " + text;
    let d = text.includes("forward")?'f':text.includes("backward")?'b':text.includes("left")?'l':text.includes("right")?'r':'s';
    fetch(`/cmd?dir=${d}&state=1`); setTimeout(()=>fetch(`/cmd?dir=${d}&state=0`), 800);
    stopMicUI();
  };
  recognition.onend = () => stopMicUI();
}
function startMic(){
  if(!isVoiceActive) return alert("Please turn ON Voice Mode first!");
  if(!recognition) return;
  const mBtn = document.getElementById("micBtn");
  let timeLeft = 5;
  mBtn.classList.add("mic-listening");
  mBtn.textContent = `LISTENING (${timeLeft}s)`;
  recognition.start();
  voiceTimeout = setTimeout(() => { recognition.stop(); stopMicUI(); }, 5000);
  clearInterval(countdownInterval);
  countdownInterval = setInterval(() => {
    timeLeft--; mBtn.textContent = timeLeft > 0 ? `LISTENING (${timeLeft}s)` : "TAP TO SPEAK";
    if(timeLeft <= 0) clearInterval(countdownInterval);
  }, 1000);
}
function stopMicUI(){
  const mBtn = document.getElementById("micBtn");
  mBtn.textContent = "TAP TO SPEAK"; mBtn.classList.remove("mic-listening");
  clearTimeout(voiceTimeout); clearInterval(countdownInterval);
}

changeMode('manual');
refreshGlobalBadge();
</script>
</body>
</html>)=====";

// ================= Motor =================
void setMotorSpeed(uint8_t l, uint8_t r) {
  ledcWrite(CH_A, l);
  ledcWrite(CH_B, r);
}

void brakeLight(bool on) {
  digitalWrite(LED_BRAKE, on ? HIGH : LOW);
}

void driveStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  setMotorSpeed(0, 0);

  // do NOT force brake light here
  // brake light only controlled by STOP button
}

void driveForward(uint8_t s) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setMotorSpeed(s, s);

  // moving -> brake off
  brakeByStopButton = false;
  brakeLight(false);
}

void driveBackward(uint8_t s) {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  setMotorSpeed(s, s);

  // moving -> brake off
  brakeByStopButton = false;
  brakeLight(false);
}

void driveLeft(uint8_t s) {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH);
  digitalWrite(IN4, LOW);
  setMotorSpeed(s, s);

  // moving -> brake off
  brakeByStopButton = false;
  brakeLight(false);
}

void driveRight(uint8_t s) {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  setMotorSpeed(s, s);

  // moving -> brake off
  brakeByStopButton = false;
  brakeLight(false);
}

// ================= Ultrasonic =================
float getDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long d = pulseIn(ECHO_PIN, HIGH, 30000);
  if (d == 0) return -1;

  return d * 0.0343 / 2;
}

void updateUltrasonic() {
  unsigned long now = millis();
  if (now - lastUltrasonicRead < ULTRA_INTERVAL) return;

  lastUltrasonicRead = now;

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long d = pulseIn(ECHO_PIN, HIGH, 25000); // shorter timeout
  if (d == 0) cachedDistance = -1;
  else cachedDistance = d * 0.0343 / 2;
}


// ================= Indicators Logic =================
void computeActiveIndicator() {
  // Headlight direct
  digitalWrite(LED_HEAD, headlightOn ? HIGH : LOW);

  // Hazard has highest priority
  if (hazardOn) {
    activeIndicator = IND_HAZARD;
    return;
  }

  // Obstacle mode auto direction indicator
  if (obstacleMode) {
    if (autoState == AUTO_TURN) {
      activeIndicator = (turnLeftNext ? IND_LEFT : IND_RIGHT);
    } else {
      activeIndicator = IND_OFF;
    }
    return;
  }

  // Follow mode no indicators
  if (followMode) {
    activeIndicator = IND_OFF;
    return;
  }

  // Manual mode buttons
  if (manualLeftInd && manualRightInd) activeIndicator = IND_HAZARD;
  else if (manualLeftInd) activeIndicator = IND_LEFT;
  else if (manualRightInd) activeIndicator = IND_RIGHT;
  else activeIndicator = IND_OFF;
}

void updateIndicatorBlink() {
  unsigned long now = millis();

  if (now - lastBlink >= BLINK_INTERVAL) {
    lastBlink = now;
    blinkState = !blinkState;
  }

  if (activeIndicator == IND_OFF) {
    digitalWrite(LED_LEFT, LOW);
    digitalWrite(LED_RIGHT, LOW);
  } else if (activeIndicator == IND_LEFT) {
    digitalWrite(LED_LEFT, blinkState ? HIGH : LOW);
    digitalWrite(LED_RIGHT, LOW);
  } else if (activeIndicator == IND_RIGHT) {
    digitalWrite(LED_LEFT, LOW);
    digitalWrite(LED_RIGHT, blinkState ? HIGH : LOW);
  } else if (activeIndicator == IND_HAZARD) {
    digitalWrite(LED_LEFT, blinkState ? HIGH : LOW);
    digitalWrite(LED_RIGHT, blinkState ? HIGH : LOW);
  }
}

// ================= Manual =================
void handleManual() {
  if (forwardCmd) driveForward(carSpeed);
  else if (backCmd) driveBackward(carSpeed);
  else if (leftCmd) driveLeft(carSpeed);
  else if (rightCmd) driveRight(carSpeed);
  else driveStop();

  // if STOP button was pressed earlier, keep brake ON while car is stopped
  if (!forwardCmd && !backCmd && !leftCmd && !rightCmd && brakeByStopButton) {
    brakeLight(true);
  }
}

// ================= Obstacle =================
void handleObstacle() {
  unsigned long now = millis();

  // brake should not turn ON in auto mode
  brakeByStopButton = false;
  brakeLight(false);

  switch (autoState) {
    case AUTO_IDLE:
      autoState = AUTO_FORWARD;
      break;

    case AUTO_FORWARD: {
      float d = cachedDistance;
      if (d > 0 && d < OBSTACLE_DIST_CM) {
        driveStop();
        autoState = AUTO_BACKWARD;
        autoStateStart = now;
      } else {
        driveForward(carSpeed);
      }
    } break;

    case AUTO_BACKWARD:
      if (now - autoStateStart >= BACK_TIME) {
        autoState = AUTO_TURN;
        autoStateStart = now;
      } else {
        driveBackward(carSpeed);
      }
      break;

    case AUTO_TURN:
      turnLeftNext ? driveLeft(carSpeed) : driveRight(carSpeed);
      if (now - autoStateStart >= TURN_TIME) {
        driveStop();
        turnLeftNext = !turnLeftNext;
        autoState = AUTO_FORWARD;
      }
      break;
  }
}

// ================= Follow =================
void handleFollow() {
  // brake should not turn ON in follow mode auto stop
  brakeByStopButton = false;
  brakeLight(false);

  float d = cachedDistance;
  if (d < 0) {
    driveStop();
    return;
  }

  if (d < 12) driveBackward(carSpeed);
  else if (d <= 20) driveStop();
  else if (d <= 45) driveForward(carSpeed);
  else driveStop();
}

// ================= Connection Status =================
void updateClientStatus() {
  unsigned long now = millis();
  clientConnected = (now - lastClientSeen) < DISCONNECT_TIMEOUT;

  if (clientConnected != prevClientConnected) {
    prevClientConnected = clientConnected;

    if (clientConnected) statusMsg = "Connected..";
    else statusMsg = "Disconnected..";

    statusMsgStart = now;
  }
}

// Add it right here, after handleFollowHttp
void handleVoice() {
  lastClientSeen = millis();

  voiceMode = server.arg("v").toInt();
  obstacleMode = false;
  followMode = false;

  // disable brake when changing modes
  brakeByStopButton = false;
  brakeLight(false);

  forwardCmd = backCmd = leftCmd = rightCmd = false;
  driveStop();

  server.send(200, "text/plain", "OK");
}

// ================= LCD DISPLAY =================
void updateLCD() {
  float dist = getDistanceCm();
  unsigned long now = millis();

  updateClientStatus();

  bool showStatusMsg = (statusMsg != "" && (now - statusMsgStart) < STATUS_MSG_TIME);

  if (!clientConnected) {
    lcd.setCursor(0, 0);
    lcd.print("Connecting....   ");
    lcd.setCursor(0, 1);
    lcd.print("Please wait....  ");
    return;
  }

  if (showStatusMsg) {
    lcd.setCursor(0, 0);
    lcd.print(statusMsg);
    lcd.print("            ");
    lcd.setCursor(0, 1);
    lcd.print("Please wait....  ");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("MODE:");
  if (followMode) lcd.print("FOL ");
  else if (obstacleMode) lcd.print("OBS ");
  else if (voiceMode) lcd.print("VOI "); // Added this
  else lcd.print("MAN ");

  lcd.print("SPD:");
  lcd.print(carSpeed);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("DIST:");
  if (dist < 0) {
    lcd.print("NA       ");
  } else {
    lcd.print(dist, 1);
    lcd.print("cm      ");
  }
}

// ================= HTTP =================
void handleRoot() {
  lastClientSeen = millis();
  server.send_P(200, "text/html", index_html);
}

void handleCmd() {
  lastClientSeen = millis();

  // BLOCK manual control in voice mode
  if (voiceMode) {
    server.send(200, "text/plain", "IGNORED");
    return;
  }

  if (obstacleMode || followMode) {
    server.send(200, "text/plain", "IGNORED");
    return;
  }

  String d = server.arg("dir");
  bool p = server.arg("state").toInt();

  if (d == "f") {
    forwardCmd = p;
    if (p) { brakeByStopButton = false; brakeLight(false); }
  }
  else if (d == "b") {
    backCmd = p;
    if (p) { brakeByStopButton = false; brakeLight(false); }
  }
  else if (d == "l") {
    leftCmd = p;
    if (p) { brakeByStopButton = false; brakeLight(false); }
  }
  else if (d == "r") {
    rightCmd = p;
    if (p) { brakeByStopButton = false; brakeLight(false); }
  }
  else if (d == "s") {
    forwardCmd = backCmd = leftCmd = rightCmd = false;
    driveStop();
    brakeByStopButton = true;
    brakeLight(true);
  }

  server.send(200, "text/plain", "OK");
}

void handleMode() {
  lastClientSeen = millis();

  obstacleMode = server.arg("auto").toInt();
  followMode = false;

  // disable brake when changing modes
  brakeByStopButton = false;
  brakeLight(false);

  autoState = AUTO_FORWARD;
  forwardCmd = backCmd = leftCmd = rightCmd = false;
  driveStop();

  server.send(200, "text/plain", "OK");
}

void handleFollowHttp() {
  lastClientSeen = millis();

  followMode = server.arg("f").toInt();
  obstacleMode = false;

  // disable brake when changing modes
  brakeByStopButton = false;
  brakeLight(false);

  forwardCmd = backCmd = leftCmd = rightCmd = false;
  driveStop();

  server.send(200, "text/plain", "OK");
}

void handleSpeed() {
  lastClientSeen = millis();

  int v = server.arg("val").toInt();
  if (v < MIN_SPEED) v = MIN_SPEED;
  if (v > MAX_SPEED) v = MAX_SPEED;

  carSpeed = (uint8_t)v;

  // FORCE immediate LCD refresh
  lastLcdUpdate = 0;
  updateLCD();

  server.send(200, "text/plain", "OK");
}


// Ping
void handlePing() {
  lastClientSeen = millis();
  server.send(200, "text/plain", "PONG");
}

// Headlight
void handleHead() {
  lastClientSeen = millis();
  headlightOn = server.arg("on").toInt();
  server.send(200, "text/plain", "OK");
}

// Indicators (manual only)
void handleInd() {
  lastClientSeen = millis();

  if (obstacleMode || followMode) {
    server.send(200, "text/plain", "IGNORED");
    return;
  }

  String side = server.arg("side");
  bool on = server.arg("on").toInt();

  if (side == "L") {
    manualLeftInd = on;
    if (on) manualRightInd = false;   // FIX: turn OFF right
  }

  if (side == "R") {
    manualRightInd = on;
    if (on) manualLeftInd = false;    // FIX: turn OFF left
  }

  server.send(200, "text/plain", "OK");
}


//Hazard (works in any mode)
void handleHaz() {
  lastClientSeen = millis();
  bool on = server.arg("on").toInt();

  hazardOn = on;

  //FIX: when hazard turns OFF, clear indicators
  if (!on) {
    manualLeftInd = false;
    manualRightInd = false;
  }

  server.send(200, "text/plain", "OK");
}


// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  // LED Pins
  pinMode(LED_LEFT, OUTPUT);
  pinMode(LED_RIGHT, OUTPUT);
  pinMode(LED_HEAD, OUTPUT);
  pinMode(LED_BRAKE, OUTPUT);

  digitalWrite(LED_LEFT, LOW);
  digitalWrite(LED_RIGHT, LOW);
  digitalWrite(LED_HEAD, LOW);
  digitalWrite(LED_BRAKE, LOW);

  // I2C init for LCD
  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  // ===== WiFi: AP + Internet =====
  WiFi.mode(WIFI_AP_STA);

  // ESP offline hotspot
  WiFi.softAP(apSsid, apPassword);

  // Connect to laptop hotspot (internet)
  WiFi.begin(staSsid, staPassword);

  Serial.print("Connecting to internet");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nInternet connected");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nInternet not available (offline mode)");
  }


  // Starting message
  lcd.setCursor(0, 0);
  lcd.print("ESP32 ROBOT CAR");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1500);
  lcd.clear();

  // Connection init
  lastClientSeen = 0;
  clientConnected = false;
  prevClientConnected = false;
  statusMsg = "";

  // Motor pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  // PWM
  ledcSetup(CH_A, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENA_PIN, CH_A);
  ledcSetup(CH_B, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENB_PIN, CH_B);

  // Ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

// If we remove the internet option then Uncomment this
  // WiFi AP
  // WiFi.mode(WIFI_AP);
  // WiFi.softAP(apSsid, apPassword);

  // Web routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/mode", handleMode);
  server.on("/follow", handleFollowHttp);
  server.on("/voice", handleVoice);
  server.on("/speed", handleSpeed);
  server.on("/ping", handlePing);

  // LED routes
  server.on("/head", handleHead);
  server.on("/ind", handleInd);
  server.on("/haz", handleHaz);

  server.begin();
}

// ================= LOOP =================
void loop() {
  updateUltrasonic();

  server.handleClient();

  // Movement logic
  if (followMode) handleFollow();
  else if (obstacleMode) handleObstacle();
  else handleManual();

  // stop car if voice loses internet
  if (voiceMode && WiFi.status() != WL_CONNECTED) {
    forwardCmd = backCmd = leftCmd = rightCmd = false;
    driveStop();
  }

  // LED logic
  computeActiveIndicator();
  updateIndicatorBlink();

  // LCD update
  if (millis() - lastLcdUpdate > LCD_INTERVAL) {
    lastLcdUpdate = millis();
    updateLCD();
  }

  delay(5);
}

