#ifndef _DIVECAN_H
#define _DIVECAN_H

#define CAN_NAME_LENGTH 8

#include "../CAN_lib/mcp_can.h"
#include <util/delay.h>
#include "../mcc_generated_files/system/system.h" // Printf
#include "string.h"
#include "CellState.h"
namespace DiveCAN
{
    using CalResult_t = struct CalResult_t
    {
        uint8_t C1_millis;
        uint8_t C2_millis;
        uint8_t C3_millis;
        uint8_t fO2;
        uint16_t pressure;
    };

    using DiveCAN_Err = enum DiveCAN_Err_t{
        LOW_BATTERY=1,
        OK = 2,
        SOLENOID = 4
    };

    using CalFunc = CalResult_t (*)(const uint8_t in_fO2, const uint16_t in_pressure_val);

    class DiveCANDevice
    {
    public:
        DiveCANDevice(byte in_canID, char *inName, CalFunc in_calFunc, uint8_t in_version);
        void NotifyPPO2(const CellState &state);
        void HandleInboundMessages(); // Event driven/interupt is hard, so keep things sequential (we have time)
        void setBatVoltage(uint8_t in_battery_voltage) { battery_voltage = in_battery_voltage;};
        void setErr(uint8_t in_err) { err = in_err;};
    protected:
        // Raw messages
        void sendID();
        void sendName();
        void sendMillis(const CellState &state);
        void sendPPO2(const CellState &state);
        void sendCellsStat(const CellState &state);
        void sendStatus();
        void sendCalAck();
        void sendCalComplete(const CalResult_t result);
        void sendMenuAck();
        void sendMenuText(byte a);
        void sendMenuFields(byte a);

    private:
        byte canID; // The bus ID we ident as
        CalFunc calFunc;
        char name[CAN_NAME_LENGTH + 1];
        MCP_CAN CAN0; // Our CAN interface
        uint8_t version;
        uint8_t battery_voltage;
        uint8_t err;
    };
}
#endif