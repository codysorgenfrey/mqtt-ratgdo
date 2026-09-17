#ifndef RATGDO_DIAGNOSTICS_H
#define RATGDO_DIAGNOSTICS_H

#include <stdint.h>

enum class RatgdoRxKind : uint8_t { Command, Status, Light, DecodeFailure };

struct RatgdoRxDiagnostic {
  uint32_t uptimeMs;
  uint16_t command;
  uint8_t value;
  uint8_t light;
  RatgdoRxKind kind;
};

constexpr uint8_t RATGDO_RX_DIAGNOSTIC_CAPACITY = 16;

// Loop-thread only. Either enable/disable call clears the FIFO and dropped count.
void setRatgdoRxDiagnosticsEnabled(bool enabled);
bool readRatgdoRxDiagnostic(RatgdoRxDiagnostic& record);
uint32_t ratgdoRxDiagnosticsDropped();

#endif
