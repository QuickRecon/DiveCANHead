#include "DigitalOxygen.h"

// Newline for terminating uart message
const char NEWLINE = 0x0D;

// Digital cell error codes
const uint16_t WARN_NEAR_SAT = 0x1;
const uint16_t ERR_LOW_INTENSITY = 0x2;
const uint16_t ERR_HIGH_SIGNAL = 0x4;
const uint16_t ERR_LOW_SIGNAL = 0x8;
const uint16_t ERR_HIGH_REF = 0x10;
const uint16_t ERR_TEMP = 0x20;
const uint16_t WARN_HUMIDITY_HIGH = 0x40;
const uint16_t WARN_PRESSURE = 0x80;
const uint16_t WARN_HUMIDITY_FAIL = 0x100;

// Cell Commands
const char *GET_OXY_COMMAND = "#DOXY";
const char *GET_DETAIL_COMMAND = "#DRAW";
const char *LOGO_COMMAND = "#DRAW";

// Time to wait on the cell to do things
const uint16_t RESPONSE_TIMEOUT = 1000; // Milliseconds, how long before the cell *definitely* isn't coming back to us

static const uint8_t uart_map[3] = {huart1, huart2, huart3};

PPO2_t getPPO2(DigitalOxygenHandle handle)
{
}

CellStatus_t sendCellCommand(char *const commandStr, char *const response)
{
    CellStatus_t status = CELL_OK;

    char inputBuffer[BUFFER_LENGTH];
    // Transmit the command to read
    size_t cmdLen = strlen(commandStr);
    while (!USART1_IsTxReady())
    {
        // Wait for the buffer to flush
    }
    for (size_t i = 0; i < cmdLen; i++)
    {
        USART1_Write(commandStr[i]);
        while (!USART1_IsTxReady())
        {
            // Wait for the buffer to flush
        }
    }
    USART1_Write(NEWLINE);

    // Wait for the response
    uint16_t timeout = 0;
    while ((!USART1_IsRxReady()) &&
           (timeout < RESPONSE_TIMEOUT))
    {
        _delay_ms(1);
        ++timeout;
    }

    if (timeout >= RESPONSE_TIMEOUT)
    { // The sensor didn't get back to us, mark as failed
        cellSample = 0;
        status = CellStatus_t::CELL_FAIL;
        printf("NO RESPONSE TO CELL MESSAGE");
    }
    else
    {
        // Recieve the response
        char lastChar = 0;
        size_t bufPos = 0;
        timeout = 0;
        while ((lastChar != NEWLINE) &&
               (bufPos < (BUFFER_LENGTH - 1)) && // Leave room for a null terminator so we can print nicely
               (timeout < DECODE_LOOPS))
        {
            if (USART1_IsRxReady())
            {
                lastChar = static_cast<char>(USART1_Read());
                inputBuffer[bufPos] = lastChar;
                ++bufPos;
            }
            _delay_us(10); // Slow down our loop just a little so we don't need a bigint to store loops
            ++timeout;
        }

        inputBuffer[bufPos] = '\0'; // Insert null terminator at end

        if (timeout >= DECODE_LOOPS)
        {
            cellSample = 0;
            status = CellStatus_t::CELL_FAIL;
            printf("DECODE TIMEOUT, got: %s\n", inputBuffer);
        }
        else
        {
            strncpy(response, inputBuffer, BUFFER_LENGTH);
        }
    }

    return status;
}
