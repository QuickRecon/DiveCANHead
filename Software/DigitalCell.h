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
    const uart_drv_interface_t digitalPortMap[] = {
        UART0,
        UART1,
        UART2};

    class DigitalCell : public ICell
    {
    public:
        explicit DigitalCell(const DigitalPort in_port);
        void sample() final;
        PPO2_t getPPO2() final;
        Millivolts_t getMillivolts() final;
        void calibrate(PPO2_t PPO2) final;

    private:
        DigitalPort port;
        uint32_t cellSample;
    };
}
#endif
