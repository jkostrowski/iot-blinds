#include <Arduino.h>
#include <SimpleTimer.h>    
#include <ESP8266WiFi.h>    
#include <ESP8266mDNS.h>    
#include <PubSubClient.h>   
#include <ArduinoOTA.h>     
#include <AH_EasyDriver.h>  
#include <TelnetStream.h>
#include <BlindsController.h>
// #include <Adafruit_INA219.h>

/******************************************************************************/

#define STEPS_TO_CLOSE            8                   // Defines the number of steps needed to open or close fully
#define STEPPER_SPEED             20                  // was 35 - Defines the speed in RPM for your stepper motor
#define STEPPER_STEPS_PER_REV     1028                // Defines the number of pulses that is required for the stepper to rotate 360 degrees
#define STEPPER_MICROSTEPPING     0                   // Defines microstepping 0 = no microstepping, 1 = 1/2 stepping, 2 = 1/4 stepping 
#define STEPPER_STEP_SIZE         80

#ifndef OLD_HW
  #define STEPPER_SLEEP_PIN       2 // D5
  #define STEPPER_DIR_PIN         0 //  D6
  #define STEPPER_STEP_PIN        3 //  D7
#else                               // D1 mini boards (blinds6, blinds9)
  #define STEPPER_SLEEP_PIN       14 // D5
  #define STEPPER_DIR_PIN         12 // D6
  #define STEPPER_STEP_PIN        13 // D7
#endif
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

const char* ssid        = WIFI_SSID; 
const char* password    = WIFI_PASS;
const char* mqtt_server = MQTT_HOST;
const int   mqtt_port   = MQTT_PORT;
const char *mqtt_user   = MQTT_USER;
const char *mqtt_pass   = MQTT_PASS;
const char *mqtt_client = MQTT_CLIENT; 

/******************************************************************************/

WiFiClient espClient;
PubSubClient mqtt(espClient);
SimpleTimer timer;
AH_EasyDriver shadeStepper(STEPPER_STEPS_PER_REV, STEPPER_DIR_PIN, STEPPER_STEP_PIN, STEPPER_MICROSTEP_1_PIN, STEPPER_MICROSTEP_2_PIN, STEPPER_SLEEP_PIN);
WiFiServer server(80);
BlindsController controller(STEPS_TO_CLOSE, STEPPER_STEP_SIZE);

char buffer[50];
bool driverOn = false;



void debug( const char* payload ) {
  Serial.println( payload );
  TelnetStream.println( payload );
}

void debug( String text ) {
  Serial.println( text );
  TelnetStream.println( text );
}

// SDK auto-connects at boot with stored credentials (default esp8266-xxxx hostname),
// so drop that session and set hostname before the DHCP request goes out
void wifi_setup() {
  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
  WiFi.disconnect();
  WiFi.hostname( MQTT_CLIENT );
  WiFi.mode(WIFI_STA);
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

void debugPosition() {
  // debug( "Position: " + String(controller.position()) + " Move: " + String(controller.pendingMove()) );
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';

  String sTopic = String(topic);
  String sPayload = String((char *)payload);

  debug("Message topic:");
  debug(topic);

  debug("Message body:");
  debug((char *) payload);

  if (sTopic == MQTT_CLIENT"/cmd") {
    CommandType result = controller.applyCommand(sPayload.c_str());
    if (result == CommandType::IgnoredMoving) {
      debug("Shade is moving, ignoring command");
      return;
    }

    if (result == CommandType::Open) {
      checkIn( "open" );
      debug( "Moving to open" );
      debugPosition();
    } 
    else if (result == CommandType::Close) {
      checkIn( "close" );
      debug( "Moving to close" );
      debugPosition();
    }
    else if (result == CommandType::Close50) {
      checkIn( "close50" );
      debug( "Moving to close50" );
      debugPosition();
    }
    else if (result == CommandType::Close110) {
      checkIn( "close110" );
      debug( "Moving to close50" );
      debugPosition();
    }
    else if (result == CommandType::CloseNeg) {
      checkIn( "-close" );
      debug( "Moving to -close" );
      debugPosition();
    }
    else if (result == CommandType::Toggle) {
      checkIn( "toggle" );
      debug( "Moving to toggle" );
      debugPosition();
    }
  }
}

void mqtt_reconnect() {
  int retries = 0;
  while (!mqtt.connected()) {
    if (retries < 10) {
      debug("Attempting MQTT connection...");
      if (mqtt.connect(mqtt_client, mqtt_user, mqtt_pass))  {
        onMqqtConnection();
      } 
      else  {
        debug("Connection failed");
        retries++;
        delay(1000);
      }
    } else {
      debug("Too many retries, giving up");
      return;
    }
  }
}

void stepperInit() {
  shadeStepper.setMicrostepping(STEPPER_MICROSTEPPING);           
  shadeStepper.setSpeedRPM(STEPPER_SPEED);                        
  shadeStepper.sleepOFF();      // enable is inverted, sleepOFF turns enable OFF
}

// Driver stays enabled for the whole move, not per chunk, to avoid repeated inrush on the 12V line
void stepperMove(int steps, boolean direction) {
  if (!driverOn) {
    shadeStepper.sleepON();     // enable is inverted
    driverOn = true;
  }
  shadeStepper.move(steps, direction);
}

void stepperRelease() {
  if (driverOn) {
    shadeStepper.sleepOFF();    // enable is inverted
    driverOn = false;
  }
}

void processStepper() {
  // debug( "stepper loop start" );
  // debug( String(controller.pendingMove()) );
  StepEvent event = controller.processStepper();
  if (event.triggered) {
    stepperMove(event.steps, event.forward ? FWD : BACK);
  } else {
    stepperRelease();
  }
}

void heartbeat() {
  checkIn( "OK" );
  publishIp();
}

void setPosition() {
  controller.calibratePosition();
}

// Run once setup

void setup() {
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  pinMode(STEPPER_STEP_PIN, OUTPUT);                // after Serial.begin, so GPIO3 is a plain output
  digitalWrite(STEPPER_STEP_PIN, LOW);
  digitalWrite(STEPPER_SLEEP_PIN, HIGH);

  stepperInit();
  
  wifi_setup();

  TelnetStream.begin();

  server.begin();

  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqtt_callback);
 

  ArduinoOTA.onStart([](){ debug("Start"); });
  ArduinoOTA.onEnd([](){ debug("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { debug("."); });
  ArduinoOTA.onError([](ota_error_t error) {  debug( "Error" ); });
  ArduinoOTA.setHostname(MQTT_CLIENT);
  ArduinoOTA.begin();
  delay(10);
  
  timer.setInterval(((1 << STEPPER_MICROSTEPPING)*5800)/STEPPER_SPEED, processStepper);   
  timer.setInterval(60000, heartbeat);

  setPosition();
}

void loop() {
  ArduinoOTA.handle();

  if (!mqtt.connected()) {
    mqtt_reconnect();
  } else {
    mqtt.loop();
  }

  timer.run();

  WiFiClient client = server.accept();
  if (client) {
    checkIn("HTTP");

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println(""); 
    client.println("<!DOCTYPE HTML>");
    client.println("<html>");
    client.println("<h1>Wellcome to " MQTT_CLIENT "</h1");
    client.println("</html>");
    client.flush();
  }
   
}
