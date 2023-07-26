#ifndef _ICELL_H
#define _ICELL_H
#include "mcc_generated_files/system/system.h"

namespace OxygenSensing
{
    using PPO2_t = uint8_t;
    using Millivolts_t = uint16_t;

    using CellStatus_t = enum class e_CellStatus {
        CELL_OK,
        CELL_DEGRADED,
        CELL_FAIL,
        CELL_NEED_CAL
    };

    // Even though this is a generic cell Shearwater still uses the
    // concept of millivolts everywhere so it carry that into all cells
    // so that it is at least handled in a non-shit way
    class Cell
    {
    public:
        virtual void sample();            // Perform the sampling operation
        virtual PPO2_t getPPO2();        // Return the sample transcribed to an 8 bit PPO2 (0xFF if CELL_FAIL)
        virtual Millivolts_t getMillivolts(); // Return the sample transcribed as a millivolts
        virtual void calibrate(PPO2_t PPO2);

        CellStatus_t getStatus() const { return status; }
        void setStatus(const CellStatus_t in_status) { status = in_status; }

    private:
        CellStatus_t status;
    };
}

#endif
