#include <Arduino.h>

#include <SimpleTimer.h>    //https://github.com/marcelloromani/Arduino-SimpleTimer/tree/master/SimpleTimer
#include <ESP8266WiFi.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <ESP8266mDNS.h>    //if you get an error here you need to install the ESP8266 board manager 
#include <PubSubClient.h>   //https://github.com/knolleary/pubsubclient
#include <ArduinoOTA.h>     //https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA
#include <AH_EasyDriver.h>  //http://www.alhin.de/arduino/downloads/AH_EasyDriver_20120512.zip

#include <TelnetStream.h>

/******************************************************************************/

//#define WIFI_SSID       ""
//#define WIFI_PASS       ""
//#define MQTT_HOST       ""
//#define MQTT_PORT       1883
//#define MQTT_USER       ""
//#define MQTT_PASS       ""
//#define MQTT_CLIENT     ""

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

WiFiServer server(80);

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
      Serial.print("Attempting MQTT connection...");
      if (mqtt.connect(mqtt_client_name, mqtt_user, mqtt_pass))  {
        Serial.println("connected");
        mqtt.publish(MQTT_CLIENT"/checkIn",boot ? "Rebooted" : "Reconnected"); 
        mqtt.subscribe(MQTT_CLIENT"/cmd"); 
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

void checkIn( const char* payload ) {
  mqtt.publish(MQTT_CLIENT"/checkIn", payload ); 
  Serial.println( payload );
  TelnetStream.println( payload );
}

void publishIp() {
  String temp_str = MQTT_CLIENT":" + WiFi.localIP().toString();
  temp_str.toCharArray(buffer, temp_str.length() + 1);
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
    // delay(10);
    // Wait until the client sends some data
    // Serial.println("new client");
    //     while(!client.available()){
    //    delay(1);
    //  }

    // Read the first line of the request
    // String request = client.readStringUntil('\r');
    // Serial.println(request);
    // client.flush();
   
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println(""); //  do not forget this one
    client.println("<!DOCTYPE HTML>");
    client.println("<html>");
    client.println("<h1>Wellcome to "MQTT_CLIENT"</h1");
//    client.println("<h1>AAA= "AAA"</h1");
//    client.println("<h1>BBB= "BBB"</h1");
//    client.println("<h1>CCC= "CCC"</h1");
    client.println("</html>");
    client.flush();
  }
   
}
