#include "service_internal.hpp"
#include "app_config.hpp"
#include "bsp_can.hpp"
#include "M2006.hpp"
#include "DJIMotorHandler.hpp"
#include <cmath>

namespace services::detail {
void control_entry(ULONG) {
    // 改成实际电机：非 C610 协议需要替换设备类和注册，不是只改参数。
    static M2006 motor{M2006::Config{app_config::motor_reduction,app_config::motor_current_limit,app_config::motor_timeout_ms}};
    const auto can0=bsp::Can(0),can1=bsp::Can(1);
    static DJIMotorHandler handler{can0,can1};
    motor.speedPid.Tuning(app_config::speed_kp,app_config::speed_ki,app_config::speed_kd);
    motor.speedPid.maxOut=app_config::motor_current_limit;
    motor.speedPid.maxIOut=app_config::speed_integral_limit;
    motor.positionPid.Tuning(app_config::position_kp,app_config::position_ki,app_config::position_kd);
    motor.positionPid.maxOut=app_config::output_speed_limit;
    motor.positionPid.maxIOut=app_config::position_integral_limit;
    bool ready=app_config::motor_hardware_confirmed &&
        handler.registerMotor(&motor,app_config::motor_bus,app_config::motor_feedback_id);
    handler.sealRegistration();
    if(ready) {
        const auto port=app_config::motor_bus==0?can0:can1;
        const auto callback=app_config::motor_bus==0?DJIMotorHandler::receiveBus0:DJIMotorHandler::receiveBus1;
        ready=port.start && port.start(port.context,callback,&handler)==bsp::Status::ok;
    }
    bool permitted=false;
    const auto revoke=[&] {
        motor.Relax();permitted=false;command_epoch.fetch_add(1);
        if(ready)(void)bsp::CanAbort(app_config::motor_bus);
    };
    uint32_t previous=now(),seenCommand=0,errors=bsp::CanErrors(app_config::motor_bus);
    uint32_t feedbackFaults=motor.feedbackFaultEpoch();
    Command cached{};
    for(;;) {
        const uint32_t time=now(),elapsed=time-previous;previous=time;
        const bool copied=tx_mutex_get(&command_lock,TX_NO_WAIT)==TX_SUCCESS;
        if(copied) {cached=command;tx_mutex_put(&command_lock);}
        const Command requested=cached; // Contention must not renew or discard the old lease.
        const bool fresh=requested.sequence&&time-requested.timestamp<app_config::command_timeout_ms;
        const uint32_t currentErrors=bsp::CanErrors(app_config::motor_bus);
        const bool wasDriving=motor.controlMode!=DJIMotor::RELAX_MODE;
        const bool online=ready&&motor.AliveCheck(time)==DJIMotor::MOTOR_ONLINE;
        if(wasDriving&&motor.controlMode==DJIMotor::RELAX_MODE)revoke();
        const bool timing=elapsed>0&&elapsed<=app_config::control_period_ms*5;
        const uint32_t epoch=motor.feedbackFaultEpoch();
        const bool newFault=epoch!=feedbackFaults||currentErrors!=errors;
        if(!online||!fresh||!timing||newFault) {
            revoke();
        }
        errors=currentErrors;
        feedbackFaults=epoch;
        if(copied&&requested.sequence!=seenCommand) {
            seenCommand=requested.sequence;
            const bool currentEpoch=requested.epoch==command_epoch.load();
            if(requested.mode==ControlMode::relax&&currentEpoch) {
                motor.Relax();(void)bsp::CanAbort(app_config::motor_bus);
                permitted=online&&fresh&&timing;
            } else if(currentEpoch&&requested.mode!=ControlMode::relax&&permitted&&online&&fresh&&timing) {
                const auto mode=requested.mode==ControlMode::speed?DJIMotor::SPD_MODE:DJIMotor::POS_MODE;
                if(motor.controlMode!=mode)motor.Relax();
                motor.controlMode=mode;
                motor.speedSet=requested.mode==ControlMode::speed?requested.target:0;
                motor.positionSet=requested.mode==ControlMode::position?requested.target:0;
                if(std::fabs(motor.speedSet)>app_config::output_speed_limit) {
                    revoke();
                }
            }
        }
        const bool driving=motor.controlMode!=DJIMotor::RELAX_MODE;
        motor.setOutput(static_cast<float>(elapsed)*.001f,time);
        // Do not let repeated target refresh re-arm a fault-relaxed module.
        if(driving&&motor.controlMode==DJIMotor::RELAX_MODE)permitted=false;
        const bool sent=ready&&handler.sendControlData(time);
        if(!sent || motor.feedbackFaultEpoch()!=feedbackFaults || (driving&&motor.controlMode==DJIMotor::RELAX_MODE)) {revoke();}
        control_healthy=ready&&online&&sent;
        beat(control_beat,control_pending);
        sleep_ms(nullptr,app_config::control_period_ms);
    }
}
}
