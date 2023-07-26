#include "DigitalCell.h"
namespace OxygenSensing
{
    DigitalCell::DigitalCell(const DigitalPort in_port) : port(in_port)
    {
        // Make sure bus is avaliable
        while (!USART1_IsTxReady())
        {
        }
        while (USART1_IsRxReady())
        {
            USART1_Read();
        }
        setStatus(CellStatus_t::CELL_OK);
    }

    void DigitalCell::sample()
    {
        char inputBuffer[86];

        // Transmit the command to read
        const char cmd[] = {'#', 'D', 'O', 'X', 'Y', 0x0D};
        for (int i = 0; i < 6; i++)
        {
            USART1_Write(cmd[i]);
            _delay_ms(10);
        }

        // TODO: Make sure we can time out on this read

        // Wait for the response
        while(!USART1_IsRxReady()){}

        // Recieve the response
        char lastChar = 0;
        int bufPos = 0;
        uint32_t loops = 0;
        while (lastChar != 0x0D && bufPos < 86)
        {
            if (USART1_IsRxReady())
            {
                lastChar = USART1_Read();
                inputBuffer[bufPos] = lastChar;
                bufPos++;
            }
            _delay_us(1);
            loops++;
        }
        inputBuffer[bufPos] = '\0'; // Insert null terminator at end

        //printf("%ld Got UART Response: %s\n", loops, inputBuffer);

        // Tokenize it
        const char *sep = " ";
        const char *CMD_name = strtok(inputBuffer, sep);
        const char *PPO2_str = strtok(NULL, sep);
        strtok(NULL, sep); // Skip temperature
        const char *err_str = strtok(NULL, sep);

        if (strcmp(CMD_name, "#DOXY") != 0) // If we don't get our cmd back then we failed hard
        {
            cellSample = 0;
            setStatus(CellStatus_t::CELL_FAIL);
        }
        else
        {
            cellSample = strtoul(PPO2_str, NULL, 10);

            int errCode = atoi(err_str);

            // Check for error states
            if (errCode == 0)
            {
                // Everything is fine
            }
            else if (errCode & (0x2 | 0x4 | 0x08 | 0x10 | 0x20))
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

    PPO2_t DigitalCell::getPPO2() // TODO: handle failed
    {
        return static_cast<PPO2_t>(cellSample / 10000);
    }
    Millivolts_t DigitalCell::getMillivolts()
    {
        return 0; // Digital cells don't have millivolts
    }

    void DigitalCell::calibrate(const PPO2_t PPO2)
    {
        // Digital cells don't need calibration :D
    }
}
