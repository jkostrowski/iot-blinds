#include <Arduino.h>

#include <Vault.h>

#include <SimpleTimer.h>    //https://github.com/marcelloromani/Arduino-SimpleTimer/tree/master/SimpleTimer
#include <ESP8266WiFi.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <ESP8266mDNS.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <PubSubClient.h>   //https://github.com/knolleary/pubsubclient
#include <ArduinoOTA.h>     //https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA
#include <AH_EasyDriver.h>  //http://www.alhin.de/arduino/downloads/AH_EasyDriver_20120512.zip

/******************************************************************************/

#define USER_MQTT_CLIENT_NAME    "blinds5"

#define STEPS_TO_CLOSE            8                   // Defines the number of steps needed to open or close fully
#define STEPPER_SPEED             20                  // was 35 - Defines the speed in RPM for your stepper motor
#define STEPPER_STEPS_PER_REV     1028                // Defines the number of pulses that is required for the stepper to rotate 360 degrees
#define STEPPER_MICROSTEPPING     0                   // Defines microstepping 0 = no microstepping, 1 = 1/2 stepping, 2 = 1/4 stepping 

#define STEPPER_SLEEP_PIN         D5
#define STEPPER_DIR_PIN           D6
#define STEPPER_STEP_PIN          D7
#define STEPPER_MICROSTEP_1_PIN   14
#define STEPPER_MICROSTEP_2_PIN   12

// default 
#define FWD                       true
#define BACK                      false

// reverse
//#define FWD                       false
//#define BACK                      true


/******************************************************************************/

WiFiClient espClient;
PubSubClient mqtt(espClient);
SimpleTimer timer;
AH_EasyDriver shadeStepper(STEPPER_STEPS_PER_REV, STEPPER_DIR_PIN, STEPPER_STEP_PIN, STEPPER_MICROSTEP_1_PIN, STEPPER_MICROSTEP_2_PIN, STEPPER_SLEEP_PIN);

//Global Variables
bool boot = true;
int currentPosition = 0;
int newPosition = 0;
char buffer[50];
bool moving = false;
char charPayload[50];

const char* ssid = USER_SSID; 
const char* password = USER_PASSWORD;
const char* mqtt_server = USER_MQTT_SERVER;
const int   mqtt_port = USER_MQTT_PORT;
const char *mqtt_user = USER_MQTT_USERNAME;
const char *mqtt_pass = USER_MQTT_PASSWORD;
const char *mqtt_client_name = USER_MQTT_CLIENT_NAME; 


void wifi_setup() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqtt_reconnect() {
  int retries = 0;
  while (!mqtt.connected()) {
    if(retries < 150) {
      Serial.print("Attempting MQTT connection...");
      if (mqtt.connect(mqtt_client_name, mqtt_user, mqtt_pass))  {
        Serial.println("connected");
        mqtt.publish(USER_MQTT_CLIENT_NAME"/checkIn",boot ? "Rebooted" : "Reconnected"); 
        mqtt.subscribe(USER_MQTT_CLIENT_NAME"/blindsCommand");
        mqtt.subscribe(USER_MQTT_CLIENT_NAME"/positionCommand");
      } 
      else  {
        Serial.print("failed, rc=");
        Serial.print(mqtt.state());
        Serial.println(" try again in 5 seconds");
        retries++;
        delay(5000);
      }
    }
    if(retries > 149) {
      ESP.restart();
    }
  }
}

void checkIn() {
  String temp_str = WiFi.localIP().toString();
  temp_str.toCharArray(buffer, temp_str.length() + 1);
  mqtt.publish(USER_MQTT_CLIENT_NAME"/checkIn", buffer); 
}

void publishPosition() {
    String temp_str = String(currentPosition);
    temp_str.toCharArray(buffer, temp_str.length() + 1);
    mqtt.publish(USER_MQTT_CLIENT_NAME"/positionState", buffer); 
}

void sleepStop() {
  shadeStepper.sleepON();
}

void sleepStart() {
  shadeStepper.sleepOFF();
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  String newTopic = topic;
  Serial.print(topic);
  Serial.print("] ");
  payload[length] = '\0';
  String newPayload = String((char *)payload);
  int intPayload = newPayload.toInt();
  Serial.println(newPayload);
  Serial.println();
  
  newPayload.toCharArray(charPayload, newPayload.length() + 1);
  
  if (newTopic == USER_MQTT_CLIENT_NAME"/blindsCommand") 
  {
    if (newPayload == "OPEN")
    {
      mqtt.publish(USER_MQTT_CLIENT_NAME"/positionCommand", "0", true);
    }
    else if (newPayload == "CLOSE")
    {   
      int stepsToClose = STEPS_TO_CLOSE;
      String temp_str = String(stepsToClose);
      temp_str.toCharArray(charPayload, temp_str.length() + 1);
      mqtt.publish(USER_MQTT_CLIENT_NAME"/positionCommand", charPayload, true);
    }
    else if (newPayload == "STOP") {
      String temp_str = String(currentPosition);
      temp_str.toCharArray(buffer, temp_str.length() + 1);
      mqtt.publish(USER_MQTT_CLIENT_NAME"/positionCommand", buffer, true); 
    }
    else if (newPayload == "UP") {   
      sleepStop();
      shadeStepper.move(80, FWD);
      sleepStart();
      mqtt.publish(USER_MQTT_CLIENT_NAME"/checkIn", "UP"); 
    }
    else if (newPayload == "DOWN") {   
      sleepStop();
      shadeStepper.move(80, BACK);
      sleepStart();
      mqtt.publish(USER_MQTT_CLIENT_NAME"/checkIn", "DOWN"); 
    }
  }

  if ((newTopic == USER_MQTT_CLIENT_NAME"/positionCommand") || (newTopic == USER_MQTT_CLIENT_NAME"/go"))
  {
    if (boot)
    {
      currentPosition = intPayload;
      boot = false;
    }
    newPosition = intPayload;
  }
}

void processStepper() {
  
  if (newPosition > currentPosition)
  {
    sleepStop();
    shadeStepper.move(80, FWD);
    currentPosition++;
    moving = true;
    sleepStart();
  }
  if (newPosition < currentPosition)
  {
    sleepStop();
    shadeStepper.move(80, BACK);
    currentPosition--;
    moving = true;
    sleepStart();
  }
  if (newPosition == currentPosition && moving == true)
  {
    publishPosition();
    moving = false;
  }
}

//Run once setup
void setup() {
  Serial.begin(115200);
  shadeStepper.setMicrostepping(STEPPER_MICROSTEPPING);           
  shadeStepper.setSpeedRPM(STEPPER_SPEED);                        
  sleepStart();
  
  WiFi.mode(WIFI_STA);
  wifi_setup();
  
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqtt_callback);
  
  ArduinoOTA.setHostname(USER_MQTT_CLIENT_NAME);
  ArduinoOTA.begin(); 
  delay(10);
  
  timer.setInterval(((1 << STEPPER_MICROSTEPPING)*5800)/STEPPER_SPEED, processStepper);   
  timer.setInterval(60000, checkIn);
}

void loop() {
  if (!mqtt.connected()) 
  {
    mqtt_reconnect();
  }
  mqtt.loop();
  
  ArduinoOTA.handle();

  timer.run();
}
