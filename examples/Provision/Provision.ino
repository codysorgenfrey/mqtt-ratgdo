#include <Arduino.h>
#include <ratgdo.h>

// Read PERSISTENCE.md before editing these deliberate maintenance settings.
constexpr bool ProvisionApproved = false;
constexpr uint32_t ControllerId = 0; // Previously-unused ID ending in 0x539.
constexpr uint32_t NextRollingCode = 0; // Zero is safe ONLY for an unused ID.

void setup() {
  Serial.begin(115200);
  delay(1000);
  if (!ProvisionApproved) {
    Serial.println("Provisioning disabled. Read PERSISTENCE.md and select a safe identity.");
    return;
  }
  if (!provisionRATGDO(ControllerId, NextRollingCode)) {
    Serial.print("Provisioning refused: ");
    Serial.println(ratgdoStorageError());
    return;
  }
  Serial.println("Identity durably provisioned. No garage commands were sent.");
}

void loop() {
  // Do not initialize the controller or run its loop in this maintenance sketch.
  delay(1000);
}
