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

#ifndef MQTT_CLIENT
  #define MQTT_CLIENT               "x1"           
#endif

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

char buffer[50];
int move = 0; 
bool moving = false;

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

void debug( String text ) {
  Serial.println( text );
  TelnetStream.println( text );
}

void wifi_setup() {
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
}

void checkIn( const char* payload ) {
  mqtt.publish(MQTT_CLIENT"/checkIn", payload ); 
  debug( payload );
}

void publishIp() {
  String temp_str = String(MQTT_CLIENT":") + WiFi.localIP().toString();
  temp_str.toCharArray(buffer, temp_str.length() + 1);
  mqtt.publish(MQTT_CLIENT"/checkIn", buffer); 
  mqtt.publish("ip", buffer ); 
}

void onMqqtConnection() { 
  checkIn( "connected" );
  publishIp();
  mqtt.subscribe(MQTT_CLIENT"/cmd"); 
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  debug("Message topic:");
  debug(topic);

  payload[length] = '\0';

  String sTopic = String(topic);
  String sPayload = String((char *)payload);
  int iPayload = sPayload.toInt();
  
  debug("Message body:");
  debug((char *)payload);

  if (sTopic == MQTT_CLIENT"/cmd") {
    checkIn( (char*) payload );
    move = iPayload;
  }
}

void mqtt_reconnect() {
  int retries = 0;
  while (!mqtt.connected()) {
    if(retries < 100) {
      debug("Attempting MQTT connection...");
      if (mqtt.connect(mqtt_client_name, mqtt_user, mqtt_pass))  {
        onMqqtConnection();
      } 
      else  {
        debug("Connection failed");
        retries++;
        delay(5000);
      }
    } else {
      ESP.restart();
      retries = 0;
    }
  }
}

void stepperInit() {
  shadeStepper.setMicrostepping(STEPPER_MICROSTEPPING);           
  shadeStepper.setSpeedRPM(STEPPER_SPEED);                        
  shadeStepper.sleepOFF();  // enable is inverted, sleep OFF turns enable OFF
}

void stepperMove(int steps, boolean direction) {
  shadeStepper.sleepON();       // enable is inverted
  shadeStepper.move(steps, direction);
  shadeStepper.sleepOFF();      // enable is inverted
}


void processStepper() {
  
  if (move > 0 ) {
    stepperMove(80, FWD);
    move--;
    moving = true;
  }
  
  if (move < 0) {
    stepperMove(80, BACK);
    move++;
    moving = true;
  }

  if (move == 0 && moving) {
    moving = false;
  }
}


void heartbeat() {
  checkIn( "OK" );
  publishIp();
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
  ArduinoOTA.handle();

  if (!mqtt.connected()) {
    mqtt_reconnect();
  }
  mqtt.loop();

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
