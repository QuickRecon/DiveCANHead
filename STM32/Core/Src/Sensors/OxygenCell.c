// Generic oxygen cell to provide a common calling convention for analog and digital cells

#include "OxygenCell.h"
#include "AnalogOxygen.h"
#include "DigitalOxygen.h"

PPO2_t analogPPO2(OxygenCell_t *self){
    return Analog_getPPO2((AnalogOxygenState_p)self->cellHandle);
}

PPO2_t digitalPPO2(OxygenCell_t *self){
    return Digital_getPPO2((DigitalOxygenState_p)self->cellHandle);
}

Millivolts_t analogMillis(OxygenCell_t *self){
    return getMillivolts((AnalogOxygenState_p)self->cellHandle);
}

Millivolts_t digitalMillis(OxygenCell_t *self){
    return 0;
}

OxygenCell_t CreateCell(uint8_t cellNumber, CellType_t type){
    OxygenCell_t cell = {0};
    cell.cellNumber = cellNumber;
    cell.type = type;
    switch(type){
        case CELL_ANALOG:
            cell.ppo2 = &analogPPO2;
            cell.millivolts = &analogMillis;
            cell.cellHandle = Analog_InitCell(cellNumber);
            break;
        case CELL_DIGITAL:
            cell.ppo2 = &digitalPPO2;
            cell.millivolts = &digitalMillis;
            cell.cellHandle = Digital_InitCell(cellNumber);
            break;
        default:
            // Panic
    }
    return cell;
}