#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    static const uint32_t FLAG_ERR_MASK = 0xFFFFFFF0u;
    typedef enum
    {
        NONE_FERR = 0,
        STACK_OVERFLOW_FERR = 1,
        MALLOC_FAIL_FERR = 2,
        HARD_FAULT_FERR = 3,
        NMI_TRIGGERED_FERR = 4,
        MEM_FAULT_FERR = 5,
        BUS_FAULT_FERR = 6,
        USAGE_FAULT_FERR = 7,
        ASSERT_FAIL_FERR = 8,
        /** @brief We ran past the end of a buffer, even if it didn't trip a hard fault we've clobbered unknown memory in an unknown way, better just to reset **/
        BUFFER_OVERRUN_FERR = 9,
        /** @brief This is like a super-duper version of UNREACHABLE_ERR that we call when we don't know if its safe to continue */
        UNDEFINED_STATE_FERR = 10,
        /** @brief The largest nonfatal error code in use, we use this to manage the flash storage of the errors **/
        MAX_FERR = UNDEFINED_STATE_FERR
    } FatalError_t;

    typedef enum
    {
        NONE_ERR = 0,

        /** @brief We weren't able to lock/unlock the flash **/
        FLASH_LOCK_ERR = 1,

        /** @brief We weren't able to read from our eeprom emulation **/
        EEPROM_ERR = 2,

        /** @brief The data we are looking at is out of date, subtly different to timeout **/
        OUT_OF_DATE_ERR = 3,

        /** @brief We failed to undertake an I2C operation **/
        I2C_BUS_ERR = 4,

        /** @brief We failed to undertake an UART operation **/
        UART_ERR = 5,

        /** @brief Check code that should be unreachable, if we got triggered here then something strange happened **/
        UNREACHABLE_ERR = 6,

        /** @brief We had an error with the os flags, can be a timeout if the flag wasn't set in time **/
        FLAG_ERR = 7,

        /** @brief Generic error, RTOS error handler got triggered **/
        CRITICAL_ERR = 8,

        /** @brief What we were waiting for never came :( **/
        TIMEOUT_ERR = 9,

        /** @brief We weren't able to successfully lodge an element in the queue **/
        QUEUEING_ERR = 10,

        /** @brief Inbound CAN message is longer than 8 bytes long **/
        CAN_OVERFLOW_ERR = 11,

        /** @brief We couldn't add the can message to the outbound buffer **/
        CAN_TX_ERR = 12,

        /** @brief The configured calibration method is not defined (config corrupt?) **/
        UNDEFINED_CAL_METHOD_ERR = 13,

        /** @brief The configured calibration method cannot complete **/
        CAL_METHOD_ERR = 14,

        /** @brief The calibration info we stored is not the calibration info that we got **/
        CAL_MISMATCH_ERR = 15,

        /** @brief The cell number can't be mapped to an input (too high?) **/
        INVALID_CELL_NUMBER_ERR = 17,

        /** @brief The adc input number can't be mapped to an input **/
        INVALID_ADC_NUMBER_ERR = 19,

        /** @brief A null pointer was passed to a function not designed to handle it **/
        NULL_PTR_ERR = 20,

        /** @brief Logging quit due to an error in FATFS **/
        LOGGING_ERR = 21,

        /** @brief Menu ended up out of bounds **/
        MENU_ERR = 22,

        /** @brief Error occurred when trying to load the config **/
        CONFIG_ERR = 23,

        /** @brief Error occured when trying to read the internal ADC */
        INT_ADC_ERR = 24,

        /** @brief We encountered an error we don't know how to handle */
        UNKNOWN_ERROR_ERR = 25,

        /** @brief A cell has reported a value that we can't display */
        CELL_OVERRANGE_ERR = 26,

        /** @brief Filesystem error has occured */
        FS_ERR = 27,

        /** @brief VBus is undervolted, cell readings are unreliable */
        VBUS_UNDER_VOLTAGE_ERR = 28,

        /** @brief The largest nonfatal error code in use, we use this to manage the flash storage of the errors **/
        MAX_ERR = VBUS_UNDER_VOLTAGE_ERR
    } NonFatalError_t;

    void NonFatalError_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName);
    void NonFatalErrorISR_Detail(NonFatalError_t error, uint32_t additionalInfo, uint32_t lineNumber, const char *fileName);
    void NonFatalError(NonFatalError_t error, uint32_t lineNumber, const char *fileName);
    void NonFatalErrorISR(NonFatalError_t error, uint32_t lineNumber, const char *fileName);

    void FatalError(FatalError_t error, uint32_t lineNumber, const char *fileName);

/* These are a bit criminal but they let me inject file and line info without having to bang out macros every time */
#define NON_FATAL_ERROR(x) (NonFatalError(x, __LINE__, __FILE__))
#define NON_FATAL_ERROR_DETAIL(x, y) (NonFatalError_Detail(x, y, __LINE__, __FILE__))
#define NON_FATAL_ERROR_ISR(x) (NonFatalErrorISR(x, __LINE__, __FILE__))
#define NON_FATAL_ERROR_ISR_DETAIL(x, y) (NonFatalErrorISR_Detail(x, y, __LINE__, __FILE__))
#define FATAL_ERROR(x) (FatalError(x, __LINE__, __FILE__))

#ifdef __cplusplus
}
#endif
