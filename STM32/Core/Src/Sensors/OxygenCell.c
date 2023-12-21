// Generic oxygen cell to provide a common calling convention for analog and digital cells

#include "OxygenCell.h"
#include "AnalogOxygen.h"
#include "DigitalOxygen.h"

typedef struct OxygenHandle_s
{
    CellType_t type;
    void *cellHandle;
} OxygenHandle_t;

StaticQueue_t CellQueues_QueueStruct[3];
uint8_t CellQueues_Storage[3][sizeof(OxygenCell_t)];
static QueueHandle_t CellQueues[3];

static OxygenHandle_t cells[3];

QueueHandle_t CreateCell(uint8_t cellNumber, CellType_t type)
{
    OxygenHandle_t *cell = &cells[cellNumber];
    CellQueues[cellNumber] = xQueueCreateStatic(1, sizeof(OxygenCell_t), CellQueues_Storage[cellNumber], &(CellQueues_QueueStruct[cellNumber]));
    cell->type = type;
    switch (type)
    {
    case CELL_ANALOG:
        cell->cellHandle = Analog_InitCell(cellNumber, CellQueues[cellNumber]);
        break;
    case CELL_DIGITAL:
        cell->cellHandle = Digital_InitCell(cellNumber, CellQueues[cellNumber]);
        break;
    default:
        // Panic
    }
    return CellQueues[cellNumber];
}
