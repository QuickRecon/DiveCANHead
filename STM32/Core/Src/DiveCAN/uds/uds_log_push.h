/**
 * @file uds_log_push.h
 * @brief UDS log message push to bluetooth client
 *
 * Implements push-based log streaming from ECU to tester (bluetooth client)
 * using UDS WDBI (Write Data By Identifier) service in an inverted pattern.
 *
 * The ECU sends WDBI frames TO the tester at address 0xFF, allowing real-time
 * log message streaming over the DiveCAN bluetooth bridge.
 *
 * @note Requires dedicated ISO-TP context (separate from request/response context)
 * @note Auto-disables after 3 consecutive transmission failures
 */

#ifndef UDS_LOG_PUSH_H
#define UDS_LOG_PUSH_H

#include <stdint.h>
#include <stdbool.h>
#include "isotp.h"

/* DIDs for log streaming are defined in uds.h:
 * UDS_DID_LOG_STREAM_ENABLE = 0xA000  - Read/Write: enable log streaming (1 byte)
 * UDS_DID_LOG_MESSAGE       = 0xA100  - Push: log message data (ECU -> Tester)
 * UDS_DID_EVENT_MESSAGE     = 0xA200  - Push: event message data (ECU -> Tester)
 * UDS_DID_STATE_VECTOR      = 0xA203  - Push: binary state vector (ECU -> Tester)
 */

/* DID for binary state vector push */
#define UDS_DID_STATE_VECTOR 0xA203U

/* Maximum log message payload size
 * TX buffer (256) - WDBI header (SID + DID = 3 bytes) */
#define UDS_LOG_MAX_PAYLOAD 253U

/* Error threshold for auto-disable */
#define UDS_LOG_ERROR_THRESHOLD 3U

/**
 * @brief Priority levels for log messages
 *
 * High priority messages (PPO2STATE, PID) are sent before low priority ones.
 * When the queue is full, low priority messages are dropped.
 */
typedef enum
{
    UDS_LOG_PRIORITY_LOW = 0,  /**< Cell samples (AnalogCell, DiveO2, O2S) - can be dropped */
    UDS_LOG_PRIORITY_HIGH = 1  /**< State messages (PPO2STATE, PID) - always sent */
} UDSLogPriority_t;

/**
 * @brief Initialize log push module
 *
 * Sets up the dedicated ISO-TP context for push operations.
 * Must be called before any other log push functions.
 *
 * @param isotpCtx Pointer to dedicated ISO-TP context for push operations
 *                 (must not be the same context used for UDS request/response)
 */
void UDS_LogPush_Init(ISOTPContext_t *isotpCtx);

/**
 * @brief Check if log streaming is enabled
 * @return true if enabled, false otherwise
 */
bool UDS_LogPush_IsEnabled(void);

/**
 * @brief Enable or disable log streaming
 *
 * When enabling, resets the error counter.
 * When disabling (or auto-disabled), no log messages will be pushed.
 *
 * @param enable true to enable, false to disable
 */
void UDS_LogPush_SetEnabled(bool enable);

/**
 * @brief Push log message to bluetooth client
 *
 * Builds a WDBI frame and sends it via ISO-TP to the tester address (0xFF).
 * This is a non-blocking call - it initiates transmission but does not wait.
 *
 * Messages are silently dropped if:
 * - Log streaming is disabled
 * - ISO-TP context is busy (previous TX in progress)
 * - Message is NULL or empty
 *
 * @param message Log message string (null terminator not sent)
 * @param length Length of message (truncated to UDS_LOG_MAX_PAYLOAD if larger)
 * @return true if push was initiated, false if dropped
 *
 * @note Does not wait for transmission to complete
 * @note Increments error counter on failure, auto-disables at threshold
 */
bool UDS_LogPush_SendLogMessage(const char *message, uint16_t length);

/**
 * @brief Push event message to bluetooth client (default low priority)
 *
 * Same as above but uses a different channel for event messages.
 * Uses low priority - may be dropped if queue is full.
 *
 * @param message Log message string (null terminator not sent)
 * @param length Length of message (truncated to UDS_LOG_MAX_PAYLOAD if larger)
 * @return true if push was initiated, false if dropped
 *
 * @note Does not wait for transmission to complete
 * @note Increments error counter on failure, auto-disables at threshold
 */
bool UDS_LogPush_SendEventMessage(const char *message, uint16_t length);

/**
 * @brief Push event message with specified priority
 *
 * High priority messages are sent before low priority ones.
 * When the queue is full:
 * - High priority: drops oldest low priority message to make room
 * - Low priority: message is dropped
 *
 * @param message Log message string (null terminator not sent)
 * @param length Length of message (truncated to UDS_LOG_MAX_PAYLOAD if larger)
 * @param priority Message priority level
 * @return true if push was initiated, false if dropped
 */
bool UDS_LogPush_SendEventMessagePrio(const char *message, uint16_t length, UDSLogPriority_t priority);

/**
 * @brief Poll for TX completion
 *
 * Call from main task loop (CANTask) to check for completed transmissions
 * and handle error counting. Handles:
 * - Successful TX completion (resets error counter)
 * - TX timeout/failure (increments error counter, auto-disables at threshold)
 *
 * @note Must be called regularly for proper error handling
 */
void UDS_LogPush_Poll(void);

/**
 * @brief Get current consecutive error count
 * @return Number of consecutive errors since last successful TX
 */
uint8_t UDS_LogPush_GetErrorCount(void);

/**
 * @brief Reset error count
 *
 * Typically called automatically on successful TX, but can be called
 * manually if needed.
 */
void UDS_LogPush_ResetErrorCount(void);

/**
 * @brief Binary state vector for efficient CAN log push
 *
 * Contains complete system state in a compact binary format.
 * Sent once per second to reduce CAN bus traffic.
 *
 * Cell type is determined from config field (bits 8-13, 2 bits per cell):
 *   - CELL_ANALOG (1): detail[0] = raw ADC value (int16 in low bits)
 *   - CELL_O2S (2): no detail fields used
 *   - CELL_DIVEO2 (3): detail[0-6] = temp, err, phase, intensity, ambientLight, pressure, humidity
 *
 * Fields ordered for natural alignment (4-byte, then 2-byte, then 1-byte).
 */
typedef struct __attribute__((packed))
{
    /* 4-byte aligned fields (112 bytes) */
    uint32_t config;           /**< Full Configuration_t bitfield (cell types in bits 8-13) */
    float consensus_ppo2;      /**< Voted PPO2 value */
    float setpoint;            /**< Current setpoint */
    float duty_cycle;          /**< Solenoid duty cycle (0.0-1.0) */
    float integral_state;      /**< PID integral accumulator */
    float cell_ppo2[3];        /**< Per-cell PPO2 (float precision from precisionPPO2) */
    uint32_t cell_detail[3][7]; /**< Per-cell detail fields (interpretation depends on cell type) */

    /* 2-byte aligned fields (4 bytes) */
    uint16_t timestamp_sec;    /**< Seconds since boot (wraps at ~18 hours) */
    uint16_t saturation_count; /**< PID saturation event counter */

    /* 1-byte fields (2 bytes) */
    uint8_t version;           /**< Protocol version (for client compatibility) */
    uint8_t cellsValid;        /**< Bit flags: which cells included in voting (bits 0-2) */
} BinaryStateVector_t;

_Static_assert(sizeof(BinaryStateVector_t) == 122, "BinaryStateVector_t size must be 122 bytes");

/**
 * @brief Push binary state vector to bluetooth client
 *
 * Sends complete system state as a single ISO-TP message.
 * Should be called once per second from PPO2TransmitterTask.
 *
 * @param state Pointer to populated state vector
 * @return true if push was initiated, false if dropped
 *
 * @note Message is silently dropped if log streaming is disabled
 * @note Uses UDS_DID_STATE_VECTOR (0xA203)
 */
bool UDS_LogPush_SendStateVector(const BinaryStateVector_t *state);

#endif /* UDS_LOG_PUSH_H */
