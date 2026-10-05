#ifndef DJIMOTORHANDLER_HPP
#define DJIMOTORHANDLER_HPP

#include "DJIMotor.hpp"
#include "bsp_ports.hpp"

// Owns no platform resources. The service owns port start/stop and scheduling.
// Port contexts, this handler, and registered motors must outlive all callbacks.
class DJIMotorHandler
{
public:
    explicit DJIMotorHandler(bsp::CanPort bus0, bsp::CanPort bus1 = {});
    DJIMotorHandler(const DJIMotorHandler &) = delete;
    DJIMotorHandler &operator=(const DJIMotorHandler &) = delete;

    // Startup-only: bus 0/1, C610 feedback IDs 0x201..0x208. Reject duplicates,
    // invalid configuration, missing send port, and motors owned by another handler.
    bool registerMotor(DJIMotor *motor, unsigned bus, uint16_t canId);
    // Call AFTER registration and BEFORE CanPort.start. No registration thereafter.
    void sealRegistration() { registrationSealed_ = true; }
    static void receiveBus0(void *context, const bsp::CanFrame &frame, uint32_t timestampMs);
    static void receiveBus1(void *context, const bsp::CanFrame &frame, uint32_t timestampMs);
    // Bounded RX entry (one serialized writer per bus); never touches PID/public state.
    // Malformed frames addressed to registered motors invalidate their feedback.
    bool updateFeedback(unsigned bus, const bsp::CanFrame &frame, uint32_t timestampMs);

    // Control-thread only. Sends occupied 0x200/0x1ff groups, unused slots zero.
    // Revalidates fault epochs immediately before each send. A frame accepted by
    // the transport cannot be recalled; faults in/after send take effect next tick.
    // Failure relaxes ALL motors and tries zero once per occupied group, no waits.
    bool sendControlData(uint32_t nowMs);
    void AllMotorAliveCheck(uint32_t nowMs);

private:
    bsp::CanPort ports_[2];
    DJIMotor *motors_[2][8]{};
    bool registrationSealed_{};
    void RelaxAll();
};

#endif // DJIMOTORHANDLER_HPP
