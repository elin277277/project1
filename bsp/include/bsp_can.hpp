#pragma once
#include "bsp_ports.hpp"
namespace bsp {
// bus: 0=CAN1, 1=CAN2; callbacks installed before peripheral starts.
CanPort Can(unsigned bus);
uint32_t CanErrors(unsigned bus);
// Nonblocking cancellation of pending mailboxes on relax/fault. A frame already
// on the wire cannot be recalled; this is not an actuator stop acknowledgement.
// The same service must own send and abort; no automatic retry/re-arm is done.
Status CanAbort(unsigned bus);
}
