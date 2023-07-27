#include "CellState.h"

namespace DiveCAN
{
    // Ok kids this is the big one, cell voting logic
    // If the shearwater starts doing bad things it probably
    // happened in here, everything else is just dragging this logic
    // into the real world.
    CellState::CellState(ICell *C1, ICell *C2, ICell *C3)
    {
        // Zeroth step, load up the millis and PPO2
        Millis[0] = C1->getMillivolts();
        Millis[1] = C3->getMillivolts();
        Millis[2] = C3->getMillivolts();

        PPO2s[0] = C1->getPPO2();
        PPO2s[1] = C2->getPPO2();
        PPO2s[2] = C3->getPPO2();

        // First check if any of the cells are begging for cal,
        // because that takes precidence over the rest of this logic
        if ((C1->getStatus() == CellStatus_t::CELL_NEED_CAL) ||
            (C2->getStatus() == CellStatus_t::CELL_NEED_CAL) ||
            (C3->getStatus() == CellStatus_t::CELL_NEED_CAL))
        {
            for (auto &PPO2 : PPO2s)
            {
                PPO2 = OxygenSensing::PPO2_FAIL; // Every cell reads as failed, prompts needs cal warning on the shearwater
            }
        }
        else
        {
            // Now for the vote itself, the logic here is to first sort the cells
            // by PPO2 and check if the max and min are more than MAX_DEVIATION apart
            CellVal_s cellVals[3] = {{0, PPO2s[0], 0x1},
                                     {1, PPO2s[1], 0x2},
                                     {2, PPO2s[2], 0x4}};

            // std::sort(cellVals, cellVals + 3, [this](CellVal_s a, CellVal_s b)
            //           { return a.PPO2 > b.PPO2; });

            qsort(cellVals, 3, sizeof(CellVal_s), CelValComp);

                // Now that the list is sorted we check upper and lower
                // Adding the corresponding bitmasks to the status
                statusMask = 0;
            uint16_t PPO2_acc = static_cast<uint16_t>(cellVals[1].PPO2); // Start an accumulator to take an average
            uint8_t includedCellCount = 1;

            // Lower cell
            // If we're outside the deviation then mark it in the mask
            // but if we're within it then add it to our average
            if ((cellVals[1].PPO2 - cellVals[0].PPO2) > MAX_DEVIATION)
            {
                statusMask |= cellVals[0].maskVal;
            }
            else
            {
                PPO2_acc += static_cast<uint16_t>(cellVals[0].PPO2);
                ++includedCellCount;
            }

            // Upper cell
            if ((cellVals[2].PPO2 - cellVals[1].PPO2) > MAX_DEVIATION)
            {
                statusMask |= cellVals[2].maskVal;
            }
            else
            {
                PPO2_acc += static_cast<uint16_t>(cellVals[3].PPO2);
                ++includedCellCount;
            }

            concensus = static_cast<PPO2_t>(PPO2_acc / includedCellCount);
        }
    }

    PPO2_t CellState::GetCellPPO2(uint8_t cellNumber) const
    {
        return PPO2s[cellNumber];
    }
    PPO2_t CellState::GetConsensusPPO2() const
    {
        return concensus;
    }

    Millivolts_t CellState::GetCellMillis(uint8_t cellNumber) const
    {
        return Millis[cellNumber];
    }

    uint8_t CellState::GetStatusMask() const
    {
        return statusMask;
    }

    int CellState::CelValComp(const void *num1, const void *num2) // comparing function
    {
        CellVal_s a = *(CellVal_s *)num1;
        CellVal_s b = *(CellVal_s *)num2;
        if (a.PPO2 > b.PPO2)
        {
            return 1;
        }
        else if (a.PPO2 < b.PPO2)
        {
            return -1;
        }
        return 0;
    }
}