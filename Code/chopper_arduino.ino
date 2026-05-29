// chopper C1-10P main code
// yahboom roboduino + arduino UNO
// esp32 sends commands over serial, this board runs everything else

#include <Wire.h>
#include <SoftwareSerial.h>
#include <Adafruit_PWMServoDriver.h>
#include <DFRobotDFPlayerMini.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

SoftwareSerial espSerial(A0, A3);
SoftwareSerial dfSerial(A2, 6);  // 1k resistor on pin 6 -> dfplayer RX
DFRobotDFPlayerMini dfPlayer;

// servos go through PCA9685 (not Servo.h) - see chopper_yahboom_ports.txt
#define S1_LEFT_LEG    0
#define S2_LEFT_FOOT   1
#define S3_RIGHT_LEG   2   // mirrored on right side
#define S4_RIGHT_FOOT  3
#define SERVO_C_HEAD   4   // head pan
#define SERVO_D_STEER  5   // center wheel steering

// M3 leg lift - ch 10/11 confirmed from bench test
#define M3_UP    10
#define M3_DOWN  11

// track motors (built into roboduino)
#define AIN1  7
#define AIN2  8
#define PWMA  11
#define BIN1  4
#define BIN2  2
#define PWMB  5

// center wheel up/down (L9110S)
#define WHEEL_UP    9
#define WHEEL_DOWN  10

#define TRIG  13
#define ECHO  12
#define STATUS_LED  A4

// angles - adjust once its built
#define LEG_UPRIGHT   90
#define LEG_FOLDED     0
#define FOOT_LEVEL    90
#define HEAD_CENTER   90
#define HEAD_MIN      45
#define HEAD_MAX     135
#define HEAD_STEP      3
#define STEER_CENTER  90
#define STEER_LEFT     0
#define STEER_RIGHT  180

#define DRIVE_SPEED   180
#define TURN_SPEED    140
#define SLOW_SPEED    100
#define WHEEL_THRESH   80
#define STOP_THRESH    60

#define STOP_WAIT     3000
#define LEG_SWEEP      700
#define WHEEL_MOVE     400
#define SONAR_CHECK    100
#define OBSTACLE_CM     30
#define SOUND_WAIT    3000

enum Mode { UPRIGHT, WHEEL };
Mode mode = UPRIGHT;
int speed = 0;
int headAngle = 90;

unsigned long stoppedAt = 0;
unsigned long lastSonar = 0;
unsigned long lastSound[7] = {0};
bool audioOk = false;


// yahboom servo formula from their example code
void setServo(int ch, int deg) {
  deg = constrain(deg, 0, 180);
  long us = (deg * 1800 / 180) + 600;
  pwm.setPWM(ch, 0, us * 4096 / 20000);
}

void setLegs(int angle) {
  setServo(S1_LEFT_LEG, angle);
  setServo(S3_RIGHT_LEG, 180 - angle);  // right leg mounted backwards
}

void setFeetLevel() {
  setServo(S2_LEFT_FOOT, FOOT_LEVEL);
  setServo(S4_RIGHT_FOOT, 180 - FOOT_LEVEL);
}

void setHead(int angle) {
  headAngle = constrain(angle, HEAD_MIN, HEAD_MAX);
  setServo(SERVO_C_HEAD, headAngle);
}

void setSteer(int angle) {
  setServo(SERVO_D_STEER, constrain(angle, 0, 180));
}

// move legs slowly from one angle to another
void sweepLegs(int from, int to, int ms) {
  int steps = abs(to - from);
  if (steps == 0) return;
  int wait = ms / steps;
  int dir = (to > from) ? 1 : -1;
  for (int a = from; a != to; a += dir) {
    setLegs(a);
    setFeetLevel();
    delay(wait);
  }
  setLegs(to);
  setFeetLevel();
}


void wheelRetract() {
  analogWrite(WHEEL_UP, 180);
  digitalWrite(WHEEL_DOWN, LOW);
  delay(WHEEL_MOVE);
  digitalWrite(WHEEL_UP, LOW);
  digitalWrite(WHEEL_DOWN, LOW);
}

void wheelDeploy() {
  digitalWrite(WHEEL_UP, LOW);
  analogWrite(WHEEL_DOWN, 180);
  delay(WHEEL_MOVE);
  digitalWrite(WHEEL_UP, LOW);
  digitalWrite(WHEEL_DOWN, LOW);
}


void leftTrack(int s) {
  if (s > 0) {
    digitalWrite(AIN1, HIGH); digitalWrite(AIN2, LOW);
    analogWrite(PWMA, s);
  } else if (s < 0) {
    digitalWrite(AIN1, LOW); digitalWrite(AIN2, HIGH);
    analogWrite(PWMA, -s);
  } else {
    digitalWrite(AIN1, LOW); digitalWrite(AIN2, LOW);
    analogWrite(PWMA, 0);
  }
}

void rightTrack(int s) {
  if (s > 0) {
    digitalWrite(BIN1, HIGH); digitalWrite(BIN2, LOW);
    analogWrite(PWMB, s);
  } else if (s < 0) {
    digitalWrite(BIN1, LOW); digitalWrite(BIN2, HIGH);
    analogWrite(PWMB, -s);
  } else {
    digitalWrite(BIN1, LOW); digitalWrite(BIN2, LOW);
    analogWrite(PWMB, 0);
  }
}

void goForward(int s) {
  speed = s;
  setSteer(STEER_CENTER);
  leftTrack(s);
  rightTrack(s);
}

void goBack(int s) {
  speed = s;
  setSteer(STEER_CENTER);
  leftTrack(-s);
  rightTrack(-s);
}

void stopDrive() {
  speed = 0;
  setSteer(STEER_CENTER);
  leftTrack(0);
  rightTrack(0);
}

void turnLeft(int s) {
  speed = s;
  setSteer(STEER_LEFT);
  leftTrack(-s);
  rightTrack(s);
}

void turnRight(int s) {
  speed = s;
  setSteer(STEER_RIGHT);
  leftTrack(s);
  rightTrack(-s);
}


void setLed(bool on) {
  digitalWrite(STATUS_LED, on ? HIGH : LOW);
}


void playSound(int track) {
  if (!audioOk || track < 1 || track > 6) return;
  if (millis() - lastSound[track] < SOUND_WAIT) return;
  lastSound[track] = millis();
  dfSerial.listen();
  dfPlayer.play(track);
  delay(50);
  espSerial.listen();  // switch back so we dont miss esp commands
}


long readSonar() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long t = pulseIn(ECHO, HIGH, 30000);
  if (t == 0) return 999;
  return t / 58;
}


void goWheelMode() {
  Serial.println("wheel mode");
  playSound(6);
  sweepLegs(LEG_UPRIGHT, LEG_FOLDED, LEG_SWEEP);
  wheelDeploy();
  mode = WHEEL;
  stoppedAt = millis();
  setLed(true);
}

void goUprightMode() {
  Serial.println("upright mode");
  playSound(6);
  stopDrive();
  wheelRetract();
  sweepLegs(LEG_FOLDED, LEG_UPRIGHT, LEG_SWEEP);
  mode = UPRIGHT;
  stoppedAt = millis();
  setLed(false);
}


void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "MOVE_FWD")        goForward(DRIVE_SPEED);
  else if (cmd == "MOVE_BWD")   goBack(DRIVE_SPEED);
  else if (cmd == "MOVE_STOP")  stopDrive();
  else if (cmd == "TURN_LEFT")  turnLeft(TURN_SPEED);
  else if (cmd == "TURN_RIGHT") turnRight(TURN_SPEED);
  else if (cmd == "MOVE_SLOW")  goForward(SLOW_SPEED);

  else if (cmd == "FACE_LEFT")   setHead(headAngle - HEAD_STEP);
  else if (cmd == "FACE_RIGHT")  setHead(headAngle + HEAD_STEP);
  else if (cmd == "FACE_CENTER") setHead(HEAD_CENTER);
  else if (cmd == "FACE_FOUND")  playSound(3);
  else if (cmd == "FACE_LOST")   setHead(HEAD_CENTER);

  else if (cmd.startsWith("PLAY_")) {
    playSound(cmd.substring(5).toInt());
  }
}


void setup() {
  Serial.begin(9600);

  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT); pinMode(PWMA, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT); pinMode(PWMB, OUTPUT);
  stopDrive();

  pinMode(WHEEL_UP, OUTPUT);
  pinMode(WHEEL_DOWN, OUTPUT);
  digitalWrite(WHEEL_UP, LOW);
  digitalWrite(WHEEL_DOWN, LOW);

  pinMode(STATUS_LED, OUTPUT);
  setLed(false);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pwm.begin();
  pwm.setPWMFreq(60);
  for (int i = 0; i < 16; i++) pwm.setPWM(i, 0, 0);

  setLegs(LEG_UPRIGHT);
  setFeetLevel();
  setHead(HEAD_CENTER);
  setSteer(STEER_CENTER);

  // dfplayer needs a sec to boot
  dfSerial.begin(9600);
  dfSerial.listen();
  delay(1500);
  if (dfPlayer.begin(dfSerial)) {
    audioOk = true;
    dfPlayer.volume(22);
    playSound(1);
  }

  espSerial.begin(9600);
  espSerial.listen();

  mode = UPRIGHT;
  speed = 0;
  stoppedAt = millis();
}


void loop() {
  espSerial.listen();
  if (espSerial.available()) {
    handleCommand(espSerial.readStringUntil('\n'));
  }

  // sonar check
  if (millis() - lastSonar >= SONAR_CHECK) {
    lastSonar = millis();
    if (readSonar() < OBSTACLE_CM) {
      stopDrive();
      playSound(5);
      espSerial.println("OBSTACLE");
      if (mode == WHEEL) goUprightMode();
    }
  }

  // switch modes based on speed
  if (mode == UPRIGHT && speed > WHEEL_THRESH) {
    goWheelMode();
  }

  if (mode == WHEEL) {
    if (speed > STOP_THRESH) {
      stoppedAt = millis();
    } else if (millis() - stoppedAt >= STOP_WAIT) {
      goUprightMode();
    }
  }
}
