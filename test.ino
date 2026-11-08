/*
  ESP-01S + DRV8825 motor test with Serial logging
  - No WiFi
  - Loop: right -> pause -> left -> pause
  - Serial is TX-only (GPIO1), so RX/GPIO3 stays free for STEP

  Pins:
    GPIO2 -> EN   (LOW = driver on; also the blue LED)
    GPIO0 -> DIR
    GPIO3 -> STEP
*/

#include <ESP8266WiFi.h>

const uint8_t PIN_EN   = 2;
const uint8_t PIN_DIR  = 0;
const uint8_t PIN_STEP = 3;

const long     STEPS_PER_MOVE      = 800;   // 1/4 rev at 1/16 microstep, 4 revs at full step
const uint32_t STEP_HALF_PERIOD_US = 500;   // 1000 steps/s
const uint32_t PAUSE_MS            = 1000;
const long     LOG_EVERY           = 100;   // progress line every N steps

long position = 0;
uint32_t cycle = 0;

void driverEnable(bool on) {
  digitalWrite(PIN_EN, on ? LOW : HIGH);
  Serial.printf("  driver %s\n", on ? "ENABLED" : "disabled");
  if (on) delay(2);
}

void moveSteps(long steps, bool right) {
  Serial.printf("[cycle %u] move %s %ld steps (pos %ld)\n",
                cycle, right ? "RIGHT" : "LEFT", steps, position);

  digitalWrite(PIN_DIR, right ? HIGH : LOW);
  Serial.printf("  DIR = %s\n", right ? "HIGH" : "LOW");
  delayMicroseconds(5);

  driverEnable(true);

  uint32_t t0 = millis();
  for (long i = 1; i <= steps; i++) {
    digitalWrite(PIN_STEP, HIGH);
    delayMicroseconds(STEP_HALF_PERIOD_US);
    digitalWrite(PIN_STEP, LOW);
    delayMicroseconds(STEP_HALF_PERIOD_US);
    position += right ? 1 : -1;

    if (i % LOG_EVERY == 0) {
      Serial.printf("  step %ld/%ld  pos %ld\n", i, steps, position);
    }
    if (i % 200 == 0) yield();   // keep the watchdog happy
  }

  Serial.printf("  done in %u ms, pos %ld\n", millis() - t0, position);
  driverEnable(false);
}

void pauseMs(uint32_t ms) {
  Serial.printf("  pause %u ms\n", ms);
  delay(ms);
}

void setup() {
  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, HIGH);        // driver off first thing
  pinMode(PIN_DIR, OUTPUT);
  digitalWrite(PIN_DIR, HIGH);

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  pinMode(PIN_STEP, OUTPUT);         // after Serial.begin, so GPIO3 is a plain output
  digitalWrite(PIN_STEP, LOW);

  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();

  delay(200);
  Serial.println("\n\n=== Blinds motor test ===");
  Serial.printf("steps/move %ld, rate %u steps/s, pause %u ms\n",
                STEPS_PER_MOVE, 1000000 / (2 * STEP_HALF_PERIOD_US), PAUSE_MS);
}

void loop() {
  cycle++;
  moveSteps(STEPS_PER_MOVE, true);
  pauseMs(PAUSE_MS);
  moveSteps(STEPS_PER_MOVE, false);
  pauseMs(PAUSE_MS);
}