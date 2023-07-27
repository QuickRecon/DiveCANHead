#ifndef _CELLSTATE_H
#define _CELLSTATE_H

#define CAN_NAME_LENGTH 8

#include "../OxygenSensing/ICell.h"

namespace DiveCAN
{
    using OxygenSensing::CellStatus_t;
    using OxygenSensing::ICell;
    using OxygenSensing::Millivolts_t;
    using OxygenSensing::PPO2_t;

    typedef struct CellVal_s
    {
        uint8_t cellNum;
        PPO2_t PPO2;
        uint8_t maskVal;
    } CellVal_s;

    constexpr uint8_t MAX_DEVIATION = 10;

    // Handles the state of each of the cells and votes for consensus
    // Provides the different parameters the DiveCAN messaging expects.
    // Because this is a diveCAN specific item, we statically deal with
    // 3 cells to make things a little simpler
    class CellState
    {
    public:
        CellState(ICell *C1, ICell *C2, ICell *C3);

        PPO2_t GetCellPPO2(uint8_t cellNumber) const;
        PPO2_t GetConsensusPPO2() const;
        Millivolts_t GetCellMillis(uint8_t cellNumber) const;
        uint8_t GetStatusMask() const;

    private:
        PPO2_t concensus;
        uint8_t statusMask;

        PPO2_t PPO2s[3] = {0, 0, 0};
        Millivolts_t Millis[3] = {0, 0, 0};

        static int CelValComp(const void *num1, const void *num2);
    };
}
#endif