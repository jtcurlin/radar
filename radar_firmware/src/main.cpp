// jacob curlin

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <Servo.h>

const int SERVO_PIN = 12;
const int TRIG_PIN  = 10;
const int ECHO_PIN  = 11;

// network details of device running visualization / controller software
const char WIFI_SSID[] = "network";    
const char WIFI_PASS[] = "password";       
const char HUB_IP[]    = "xxx.xxx.xxx.xxx"; 

const int RADAR_DATA_PORT    = 8888; // port to send detection data to
const int RADAR_COMMAND_PORT = 8889; // port to listen for commands on

// global objects
Servo radarServo;
WiFiUDP Udp;
int status = WL_IDLE_STATUS;

void checkForCommands() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    char packetBuffer[255];
    int len = Udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }
    Serial.print("Received command: '");
    Serial.print(packetBuffer);
    Serial.println("'");
    
    // todo: add logic to act on commands.
  }
}

// (helper fn) read distance from ultrasonic sensor
// > returns normalized distance [0.0, 1.0] (where 1.0 := MAX_RANGE_CM)
float readDistance() {
  const float MAX_RANGE_CM = 100.0; // max effective range ~1 meter

  // trigger sensor
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // read echo pulse duration in us
  long duration = pulseIn(ECHO_PIN, HIGH);

  // calculate distance in cm
  // speed of sound 0.0343 cm/us.
  // pulse travels there & back -> divide by 2.
  float distance_cm = (duration * 0.0343) / 2.0;

  // normalize distance to 0.0 - 1.0 range
  if (distance_cm > MAX_RANGE_CM || distance_cm <= 0) {
    return 1.0; // max range if oob
  }
  return distance_cm / MAX_RANGE_CM;
}

void radar_setup() {
  Serial.begin(115200);
  while (!Serial);

  // init hardware
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  radarServo.attach(SERVO_PIN);
  radarServo.write(90); // start @ center pos
  delay(500);

  // init wifi
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(WIFI_SSID);
    status = WiFi.begin(WIFI_SSID, WIFI_PASS);
    delay(5000);
  }
  Serial.println("\nConnected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // init udp
  Udp.begin(RADAR_COMMAND_PORT);
  Serial.print("Listening for commands on port ");
  Serial.println(RADAR_COMMAND_PORT);
}

void loop() {
  // forward sweep
  for (int angle = 0; angle <= 180; angle++) {
    radarServo.write(angle);
    
    float distance = readDistance();
    Udp.beginPacket(HUB_IP, RADAR_DATA_PORT);
    Udp.print(String(angle) + "," + String(distance));
    Udp.endPacket();

    // checkForCommands();
    
    delay(20); // (determines sweep speed)
  }

  // backward sweep
  for (int angle = 180; angle >= 0; angle--) {
    radarServo.write(angle);

    float distance = readDistance();
    Udp.beginPacket(HUB_IP, RADAR_DATA_PORT);
    Udp.print(String(angle) + "," + String(distance));
    Udp.endPacket();
    
    // checkForCommands();
    
    delay(20); // (determines sweep speed)
  }
}
