#include <Arduino.h>
#include <ratgdo.h>

void setup() {
  delay(20000); // Wait for 20 seconds to connect to Serial Monitor
  Serial.begin(115200);
  while (!Serial && millis() < 30000) {
    // wait for serial port to connect. Needed for native USB
  }
  
  if (!setupRATGDO()) {
    Serial.print("RATGDO receive-only: ");
    Serial.println(ratgdoStorageError());
  }
}

void loop() {
  loopRATGDO();
}