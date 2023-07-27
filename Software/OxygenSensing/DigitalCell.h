#ifndef _DIGITALCELL_H
#define _DIGITALCELL_H

#include "ICell.h"

#include <util/delay.h>

namespace OxygenSensing
{
    // Link our class to real hardware
    using DigitalPort = enum class e_DigitalPort {
        C1 = 0,
        C2 = 1,
        C3 = 2
    };

    // ADC port mapping
    const uart_drv_interface_t digitalPortMap[3] = {
        UART0,
        UART1,
        UART2};

    // Newline for terminating uart message
    constexpr char NEWLINE = 0x0D;

    // Digital cell error codes
    constexpr uint16_t WARN_NEAR_SAT = 0x1;
    constexpr uint16_t ERR_LOW_INTENSITY = 0x2;
    constexpr uint16_t ERR_HIGH_SIGNAL= 0x4;
    constexpr uint16_t ERR_LOW_SIGNAL = 0x8;
    constexpr uint16_t ERR_HIGH_REF = 0x10;
    constexpr uint16_t ERR_TEMP = 0x20;
    constexpr uint16_t WARN_HUMIDITY_HIGH = 0x40;
    constexpr uint16_t WARN_PRESSURE = 0x80;
    constexpr uint16_t WARN_HUMIDITY_FAIL = 0x100;

    // Time to wait on the cell to do things
    constexpr uint16_t RESPONSE_TIMEOUT = 1000; // Milliseconds, how long before the cell *defininitely* isn't coming back to us
    constexpr uint16_t DECODE_LOOPS = 5'000; // Number of loops before we give up on the message coming in;

    // Implementation consts
    constexpr uint8_t BUFFER_LENGTH = 86;
    constexpr uint16_t HPA_PER_BAR = 10'000;
    constexpr uint8_t PPO2_BASE = 10;
    class DigitalCell : public ICell
    {
    public:
        explicit DigitalCell(const DigitalPort in_port);
        void sample() final;
        PPO2_t getPPO2() final;
        Millivolts_t getMillivolts() final;
        void calibrate(PPO2_t PPO2) final;

    private:
        void decodeResponse(char (&inputBuffer)[BUFFER_LENGTH]);
        DigitalPort port; // Not used for now, because fixed digital out
        uint32_t cellSample = 0;
    };
}
#endif
