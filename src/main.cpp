#include <Arduino.h>
#include <SimpleTimer.h>    
#include <ESP8266WiFi.h>    
#include <ESP8266mDNS.h>    
#include <PubSubClient.h>   
#include <ArduinoOTA.h>     
#include <AH_EasyDriver.h>  

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

const char* ssid = WIFI_SSID; 
const char* password = WIFI_PASS;
const char* mqtt_server = MQTT_HOST;
const int   mqtt_port = MQTT_PORT;
const char *mqtt_user = MQTT_USER;
const char *mqtt_pass = MQTT_PASS;
const char *mqtt_client_name = MQTT_CLIENT; 

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
        mqtt.publish(MQTT_CLIENT"/checkIn",boot ? "Rebooted" : "Reconnected"); 
        mqtt.subscribe(MQTT_CLIENT"/blindsCommand");
        mqtt.subscribe(MQTT_CLIENT"/positionCommand");
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
  mqtt.publish(MQTT_CLIENT"/checkIn", buffer); 
}

void publishPosition() {
    String temp_str = String(currentPosition);
    temp_str.toCharArray(buffer, temp_str.length() + 1);
    mqtt.publish(MQTT_CLIENT"/positionState", buffer); 
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
  
  if (newTopic == MQTT_CLIENT"/blindsCommand") 
  {
    if (newPayload == "OPEN")
    {
      mqtt.publish(MQTT_CLIENT"/positionCommand", "0", true);
    }
    else if (newPayload == "CLOSE")
    {   
      int stepsToClose = STEPS_TO_CLOSE;
      String temp_str = String(stepsToClose);
      temp_str.toCharArray(charPayload, temp_str.length() + 1);
      mqtt.publish(MQTT_CLIENT"/positionCommand", charPayload, true);
    }
    else if (newPayload == "STOP") {
      String temp_str = String(currentPosition);
      temp_str.toCharArray(buffer, temp_str.length() + 1);
      mqtt.publish(MQTT_CLIENT"/positionCommand", buffer, true); 
    }
    else if (newPayload == "UP") {   
      sleepStop();
      shadeStepper.move(80, FWD);
      sleepStart();
      mqtt.publish(MQTT_CLIENT"/checkIn", "UP"); 
    }
    else if (newPayload == "DOWN") {   
      sleepStop();
      shadeStepper.move(80, BACK);
      sleepStart();
      mqtt.publish(MQTT_CLIENT"/checkIn", "DOWN"); 
    }
  }

  if ((newTopic == MQTT_CLIENT"/positionCommand") || (newTopic == MQTT_CLIENT"/go"))
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
  
  ArduinoOTA.setHostname(MQTT_CLIENT);
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
