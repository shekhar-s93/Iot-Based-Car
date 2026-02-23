#include <WiFi.h>
#include <WebServer.h>

// ===== LCD 16x2 I2C =====
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ================= WiFi =================
const char* apSsid = "ESP";
const char* apPassword = "326681285577";
WebServer server(80);

// ================= L298N =================
#define IN1 14
#define IN2 27
#define IN3 26
#define IN4 25
#define ENA_PIN 33
#define ENB_PIN 32

// ================= PWM =================
const int PWM_FREQ = 5000;
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
const unsigned long DISCONNECT_TIMEOUT = 8000; //  8 seconds

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
<style>
body{background:#111;color:#fff;font-family:Arial;text-align:center}
.btn{padding:16px 22px;margin:6px;border:none;border-radius:10px;background:#333;color:#fff;font-size:15px}
.btn:active{background:#555}
.sliderBox{width:90%;max-width:420px;margin:auto;padding:10px;background:#222;border-radius:12px}
input[type=range]{width:100%}
.value{font-size:18px;margin-top:8px}
.small{font-size:12px;color:#aaa}
hr{border:0;height:1px;background:#333;margin:16px 0}
</style>
</head>
<body>

<h2>Welcome to ESP car Remote.</h2>

<button class="btn" onmousedown="cmd('f',1)" onmouseup="cmd('f',0)" ontouchstart="cmd('f',1)" ontouchend="cmd('f',0)">Forward</button><br>

<button class="btn" onmousedown="cmd('l',1)" onmouseup="cmd('l',0)" ontouchstart="cmd('l',1)" ontouchend="cmd('l',0)">Left</button>
<button class="btn" onclick="cmd('s',1)">Stop</button>
<button class="btn" onmousedown="cmd('r',1)" onmouseup="cmd('r',0)" ontouchstart="cmd('r',1)" ontouchend="cmd('r',0)">Right</button><br>

<button class="btn" onmousedown="cmd('b',1)" onmouseup="cmd('b',0)" ontouchstart="cmd('b',1)" ontouchend="cmd('b',0)">Backward</button>

<br><br>

<div class="sliderBox">
  <h3>Speed Control (All Modes)</h3>
  <input type="range" min="160" max="255" value="160" id="spd" oninput="setSpeed(this.value)">
  <div class="value">Speed: <span id="spdVal">160</span></div>
  <div class="small">Range: 160 - 255</div>
</div>

<hr>

<h3>Lights</h3>

<button class="btn" onclick="toggleHead(1)">Headlight ON</button>
<button class="btn" onclick="toggleHead(0)">Headlight OFF</button><br>

<button class="btn" onclick="toggleInd('L',1)">Left ON</button>
<button class="btn" onclick="toggleInd('L',0)">Left OFF</button><br>

<button class="btn" onclick="toggleInd('R',1)">Right ON</button>
<button class="btn" onclick="toggleInd('R',0)">Right OFF</button><br>

<button class="btn" onclick="haz(1)">HAZARD ON</button>
<button class="btn" onclick="haz(0)">HAZARD OFF</button>

<hr>

<h3>Obstacle Avoid Mode</h3>
<button class="btn" onclick="mode(1)">ON</button>
<button class="btn" onclick="mode(0)">OFF</button>

<hr>

<h3>Follow Mode</h3>
<button class="btn" onclick="follow(1)">ON</button>
<button class="btn" onclick="follow(0)">OFF</button>

<script>
function cmd(d,s){ fetch(`/cmd?dir=${d}&state=${s}`); }
function mode(v){ fetch(`/mode?auto=${v}`); }
function follow(v){ fetch(`/follow?f=${v}`); }

function setSpeed(v){
  document.getElementById("spdVal").innerText = v;
  fetch(`/speed?val=${v}`);
}

// Headlight
function toggleHead(v){ fetch(`/head?on=${v}`); }

// Indicator L/R
function toggleInd(side, v){ fetch(`/ind?side=${side}&on=${v}`); }

// Hazard
function haz(v){ fetch(`/haz?on=${v}`); }

// HEARTBEAT PING (every 1 sec)
setInterval(()=>{ fetch('/ping'); }, 1000);
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

  unsigned long d = pulseIn(ECHO_PIN, HIGH, 6000); // shorter timeout
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

  if (forwardCmd && leftCmd)        driveLeft(carSpeed);
  else if (forwardCmd && rightCmd)  driveRight(carSpeed);

  // 🔴 FIXED reverse steering
  else if (backCmd && leftCmd)      driveRight(carSpeed);
  else if (backCmd && rightCmd)     driveLeft(carSpeed);

  else if (forwardCmd)              driveForward(carSpeed);
  else if (backCmd)                 driveBackward(carSpeed);
  else if (leftCmd)                 driveLeft(carSpeed);
  else if (rightCmd)                driveRight(carSpeed);
  else driveStop();

  if (!forwardCmd && !backCmd && !leftCmd && !rightCmd && brakeByStopButton)
    brakeLight(true);
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

      if (turnLeftNext) driveLeft(carSpeed);
      else driveRight(carSpeed);

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

  static int followState = 0; // -1 back, 0 stop, 1 forward
  float d = cachedDistance;

  if (d < 0) {
    driveStop();
    return;
  }

    // stable tracking (no dead zone freeze)
  if (d <= 18) followState = -1;
  else if (d >= 35) followState = 1;
  else followState = 0;


  if (followState == -1) driveBackward(carSpeed);
  else if (followState == 1) driveForward(carSpeed);
  else driveStop();
}



// ================= Connection Status =================
void updateClientStatus() {
  unsigned long now = millis();
  clientConnected = (now - lastClientSeen) < DISCONNECT_TIMEOUT;

  if (clientConnected != prevClientConnected) {

    if (clientConnected) {
      statusMsg = "Connected..";
    } else {
      statusMsg = "Disconnected..";

      // SAFETY STOP
      forwardCmd = backCmd = leftCmd = rightCmd = false;
      obstacleMode = false;
      followMode = false;
      driveStop();
      brakeLight(true);
    }

    prevClientConnected = clientConnected;
    statusMsgStart = now;
  }
}



// ================= LCD DISPLAY =================
void updateLCD() {
  float dist = cachedDistance;
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
    // STOP button pressed -> brake light ON
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

  brakeByStopButton = false;
  digitalWrite(LED_BRAKE, LOW);

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

  brakeByStopButton = false;
  digitalWrite(LED_BRAKE, LOW);

  driveStop();

  server.send(200, "text/plain", "OK");
}

void handleSpeed() {
  lastClientSeen = millis();

  int v = server.arg("val").toInt();
  if (v < MIN_SPEED) v = MIN_SPEED;
  if (v > MAX_SPEED) v = MAX_SPEED;

  carSpeed = (uint8_t)v;
  
  if(forwardCmd || backCmd || leftCmd || rightCmd || obstacleMode || followMode)
  setMotorSpeed(carSpeed, carSpeed);

  // FORCE immediate LCD refresh
  lastLcdUpdate = 0;
  updateLCD();

  server.send(200, "text/plain", "OK");
}


// Ping
void handlePing() {
  lastClientSeen = millis();

  // force connection active immediately
  if (!clientConnected) {
    forwardCmd = backCmd = leftCmd = rightCmd = false;
    obstacleMode = false;
    followMode = false;
    driveStop();
  }

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

  // Starting message
  lcd.setCursor(0, 0);
  lcd.print("ESP32 ROBOT CAR");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1500);
  lcd.clear();

  // Connection init
  lastClientSeen = millis();
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

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPassword);

  // Web routes
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/mode", handleMode);
  server.on("/follow", handleFollowHttp);
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
  updateClientStatus(); 
  updateUltrasonic();

  server.handleClient();

  // Movement logic
  if (followMode) handleFollow();
  else if (obstacleMode) handleObstacle();
  else handleManual();

  // LED logic
  computeActiveIndicator();
  updateIndicatorBlink();

  // LCD update
  if (millis() - lastLcdUpdate > LCD_INTERVAL) {
    lastLcdUpdate = millis();
    updateLCD();
  }

}
