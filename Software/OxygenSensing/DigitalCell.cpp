#include "DigitalCell.h"
namespace OxygenSensing
{
    DigitalCell::DigitalCell(const DigitalPort in_port) : port(in_port)
    {
        // Make sure bus is avaliable
        while (!USART1_IsTxReady())
        {
            // Wait for the buffer to flush
        }
        while (USART1_IsRxReady())
        {
            USART1_Read();
        }
        setStatus(CellStatus_t::CELL_OK);
    }

    void DigitalCell::sample()
    {
        char inputBuffer[BUFFER_LENGTH];

        // Transmit the command to read
        const uint8_t cmd[] = {'#', 'D', 'O', 'X', 'Y', NEWLINE};
        //for (size_t i = 0; i < sizeof(cmd); ++i)
        for(const auto cmd_char: cmd)
        {
            while (!USART1_IsTxReady())
            {
                // Wait for the buffer to flush
            }
            USART1_Write(cmd_char);
        }

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
            setStatus(CellStatus_t::CELL_FAIL);
            printf("NO RESPONSE TO CELL MESSAGE");
        }
        else
        {
            // Recieve the response
            char lastChar = 0;
            size_t bufPos = 0;
            timeout = 0;
            while ((lastChar != NEWLINE) &&
                   (bufPos < (BUFFER_LENGTH-1)) && // Leave room for a null terminator so we can print nicely
                   (timeout < DECODE_LOOPS))
            {
                if (USART1_IsRxReady())
                {
                    lastChar = static_cast<char>(USART1_Read());
                    inputBuffer[bufPos] = lastChar;
                    ++bufPos;
                }
                _delay_us(1); // Slow down our loop just a little so we don't need a bigint to store loops
                ++timeout;
            }

            inputBuffer[bufPos] = '\0'; // Insert null terminator at end

            if (timeout >= DECODE_LOOPS)
            {
                cellSample = 0;
                setStatus(CellStatus_t::CELL_FAIL);
                printf("DECODE TIMEOUT, got: %s\n", inputBuffer);
            }
            else
            {
                printf("Got UART Response: %s\n", inputBuffer);

                decodeResponse(inputBuffer);
            }
        }
    }

    PPO2_t DigitalCell::getPPO2()
    {
        PPO2_t PPO2 = 0;
        if ((getStatus() == CellStatus_t::CELL_FAIL) || (getStatus() == CellStatus_t::CELL_NEED_CAL))
        {
            PPO2 = PPO2_FAIL; // Failed cell
        }
        else
        {
            PPO2 = static_cast<PPO2_t>(cellSample / HPA_PER_BAR);
        }
        return PPO2;
    }

    Millivolts_t DigitalCell::getMillivolts()
    {
        return 0; // Digital cells don't have millivolts
    }

    void DigitalCell::calibrate(const PPO2_t PPO2)
    {
        // Digital cells don't need calibration :D
    }

    void DigitalCell::decodeResponse(char (&inputBuffer)[BUFFER_LENGTH])
    {
        // Tokenize it
        const char *const sep = " ";
        const char *const CMD_name = strtok(inputBuffer, sep);
        const char *const PPO2_str = strtok(nullptr, sep);
        strtok(nullptr, sep); // Skip temperature
        const char *const err_str = strtok(nullptr, sep);

        if (strcmp(CMD_name, "#DOXY") != 0) // If we don't get our cmd back then we failed hard
        {
            cellSample = 0;
            setStatus(CellStatus_t::CELL_FAIL);
        }
        else
        {
            cellSample = strtoul(PPO2_str, nullptr, PPO2_BASE);

            const auto errCode = static_cast<uint16_t>(strtol(err_str,nullptr, PPO2_BASE));

            // Check for error states
            if (0 == errCode)
            {
                // Everything is fine
            }
            else if ((errCode &
                      (ERR_LOW_INTENSITY |
                       ERR_HIGH_SIGNAL |
                       ERR_LOW_SIGNAL |
                       ERR_HIGH_REF |
                       ERR_TEMP)) != 0)
            {
                // Fatal errors
                setStatus(CellStatus_t::CELL_FAIL);
            }
            else
            {
                // Nonfatal errors
                setStatus(CellStatus_t::CELL_DEGRADED);
            }
        }
    }
}
