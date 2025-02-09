#include <Arduino.h>
#include <SimpleTimer.h>    
#include <ESP8266WiFi.h>    
#include <ESP8266mDNS.h>    
#include <PubSubClient.h>   
#include <ArduinoOTA.h>     
#include <AH_EasyDriver.h>  
#include <TelnetStream.h>
// #include <Adafruit_INA219.h>

/******************************************************************************/

#define STEPS_TO_CLOSE            8                   // Defines the number of steps needed to open or close fully
#define STEPPER_SPEED             20                  // was 35 - Defines the speed in RPM for your stepper motor
#define STEPPER_STEPS_PER_REV     1028                // Defines the number of pulses that is required for the stepper to rotate 360 degrees
#define STEPPER_MICROSTEPPING     0                   // Defines microstepping 0 = no microstepping, 1 = 1/2 stepping, 2 = 1/4 stepping 

#define STEPPER_SLEEP_PIN         D5
#define STEPPER_DIR_PIN           D6
#define STEPPER_STEP_PIN          D7
#define STEPPER_MICROSTEP_1_PIN   14
#define STEPPER_MICROSTEP_2_PIN   12

#ifndef REVERSE
  #define FWD                       true
  #define BACK                      false
#else
  #define FWD                       false
  #define BACK                      true
#endif

/******************************************************************************/

WiFiClient espClient;
PubSubClient mqtt(espClient);
SimpleTimer timer;
AH_EasyDriver shadeStepper(STEPPER_STEPS_PER_REV, STEPPER_DIR_PIN, STEPPER_STEP_PIN, STEPPER_MICROSTEP_1_PIN, STEPPER_MICROSTEP_2_PIN, STEPPER_SLEEP_PIN);

WiFiServer server(80);
// Adafruit_INA219 ina219;

//Global Variables
bool boot = true;
bool calibrate = false;
int currentPosition = 0;
int newPosition = 0;
char buffer[50];
bool moving = false;
char charPayload[50];

const char* ssid = WIFI_SSID; 
const char* password = WIFI_PASS;
const char* mqtt_server = MQTT_HOST;
const int   mqtt_port = MQTT_PORT;
const char *mqtt_user = MQTT_USER;
const char *mqtt_pass = MQTT_PASS;
const char *mqtt_client_name = MQTT_CLIENT; 


void debug( const char* payload ) {
  Serial.println( payload );
  TelnetStream.println( payload );
}


void wifi_setup() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

void mqtt_reconnect() {
  int retries = 0;
  while (!mqtt.connected()) {
    if(retries < 150) {
      debug("Attempting MQTT connection...");
      if (mqtt.connect(mqtt_client_name, mqtt_user, mqtt_pass))  {
        debug("connected");
        mqtt.publish(MQTT_CLIENT"/checkIn",boot ? "Rebooted" : "Reconnected"); 
        mqtt.subscribe(MQTT_CLIENT"/cmd"); 
      } 
      else  {
        debug("connection failed");
        // Serial.print("failed, rc=");
        // Serial.print(mqtt.state());
        retries++;
        delay(5000);
      }
    }
    if(retries > 149) {
      ESP.restart();
    }
  }
}

void checkIn( const char* payload ) {
  mqtt.publish(MQTT_CLIENT"/checkIn", payload ); 
  Serial.println( payload );
  TelnetStream.println( payload );
}

void publishIp() {
  String temp_str = MQTT_CLIENT":" + WiFi.localIP().toString();
  temp_str.toCharArray(buffer, temp_str.length() + 1);
  mqtt.publish(MQTT_CLIENT"/checkIn", buffer); 
  mqtt.publish("ip", buffer ); 
}

void publishPosition() {
  String temp_str = String(currentPosition);
  temp_str.toCharArray(buffer, temp_str.length() + 1);
  mqtt.publish(MQTT_CLIENT"/pos", buffer); 
}

void stepperInit() {
  shadeStepper.setMicrostepping(STEPPER_MICROSTEPPING);           
  shadeStepper.setSpeedRPM(STEPPER_SPEED);                        
  // enable is inverted, sleep OFF turns enable OFF
  shadeStepper.sleepOFF();
}

void stepperMove(int steps, boolean direction) {
  // enable is inverted
  shadeStepper.sleepON();  
  shadeStepper.move(steps, direction);
  shadeStepper.sleepOFF();
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  debug("Message topic:");
  debug(topic);

  payload[length] = '\0';

  String newTopic = topic;
  String newPayload = String((char *)payload);
  int intPayload = newPayload.toInt();
  
  newPayload.toCharArray(charPayload, newPayload.length() + 1);

  debug("Message body:");
  debug(charPayload);

  if (newTopic == MQTT_CLIENT"/cmd") {
    
    if (newPayload == "OPEN") {
      intPayload = 0;
      checkIn( "OPEN" );
    }
    else if (newPayload == "CLOSE") {   
      intPayload = STEPS_TO_CLOSE;
      checkIn( "CLOSE" );
    }
    else if (newPayload == "STOP") {
      intPayload = currentPosition;
      checkIn( "STOP" );
    }
    else if (newPayload == "TOGGLE") {   
      intPayload = (currentPosition < STEPS_TO_CLOSE/2 ) ? STEPS_TO_CLOSE : 0;
      checkIn( "TOGGLE" );
    }
    else if (newPayload == "UP") {   
      stepperMove(80, FWD);
      checkIn( "UP" );
    }
    else if (newPayload == "DOWN") {   
      stepperMove(80, BACK);
      checkIn( "DOWN" );
    }
    else if (newPayload == "XXX") {   
      calibrate = true;
      intPayload = STEPS_TO_CLOSE + 1;
      checkIn( "XXX" );
    }
  }

  if (boot)
  {
    currentPosition = intPayload;
    boot = false;
  }

  newPosition = intPayload;
}

void processStepper() {
  
  if (newPosition > currentPosition) {
    stepperMove(80, FWD);
    currentPosition++;
    moving = true;
  }
  
  if (newPosition < currentPosition) {
    stepperMove(80, BACK);
    currentPosition--;
    moving = true;
  }

  if (newPosition == currentPosition && moving) {
    if (calibrate) {
      newPosition--;
      currentPosition--;
      calibrate = false;
      checkIn("calibrated");
    }

    moving = false;

    publishPosition();
   }
}


// void reportVoltage() {
//   char buffer[128];

//   float shuntvoltage = 0;
//   float busvoltage = 0;
//   float current_mA = 0;
//   float loadvoltage = 0;
//   float power_mW = 0;

//   shuntvoltage = ina219.getShuntVoltage_mV();
//   busvoltage = ina219.getBusVoltage_V();
//   current_mA = ina219.getCurrent_mA();
//   power_mW = ina219.getPower_mW();
//   loadvoltage = busvoltage + (shuntvoltage / 1000);
  
//   sprintf(buffer, "Bus Voltage  : %.2f V" , busvoltage); debug(buffer);
//   sprintf(buffer, "Shunt Voltage: %.2f mV", shuntvoltage); debug(buffer);
//   sprintf(buffer, "Load Voltage : %.2f V" , loadvoltage); debug(buffer);
//   sprintf(buffer, "Current      : %.2f mA", current_mA); debug(buffer);
//   sprintf(buffer, "Power        : %.2f mW", power_mW);  debug(buffer);
//   debug( "--------------------------------------------------");
// }

void heartbeat() {
  checkIn( "OK" );
  publishIp();
  publishPosition();
}


//Run once setup
void setup() {
  Serial.begin(115200);

  stepperInit();
  
  WiFi.mode(WIFI_STA);
  wifi_setup();

  TelnetStream.begin();

  server.begin();

  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqtt_callback);
 
  ArduinoOTA.setHostname(MQTT_CLIENT);
  ArduinoOTA.begin(); 
  delay(10);

  // if (! ina219.begin()) {
  //   debug("Failed to find INA219 chip");
  //   checkIn("INA219 not found");
  // } 
  // else {
  //   checkIn("INA219 found");
  //   ina219.setCalibration_16V_400mA();
  //   timer.setInterval(10000, reportVoltage);
  // }
  
  timer.setInterval(((1 << STEPPER_MICROSTEPPING)*5800)/STEPPER_SPEED, processStepper);   
  timer.setInterval(60000, heartbeat);
}

void loop() {
  if (!mqtt.connected()) {
    mqtt_reconnect();
  }
  mqtt.loop();
  
  ArduinoOTA.handle();

  timer.run();

  WiFiClient client = server.available();
  if (client) {
    checkIn("HTTP");

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println(""); 
    client.println("<!DOCTYPE HTML>");
    client.println("<html>");
    client.println("<h1>Wellcome to "MQTT_CLIENT"</h1");
    client.println("</html>");
    client.flush();
  }
   
}
