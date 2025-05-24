/** \file PPO2Control.c
 *  \author Aren Leishman
 *  \brief This file contains all of the logic for managing the setpoint, solenoid PID loop, and associated state.
 */
#include "PPO2Control.h"
#include "../Hardware/solenoid.h"
#include "../Sensors/OxygenCell.h"
#include "../errors.h"
#include "../Hardware/log.h"

static PPO2_t setpoint = 70;

typedef struct
{
    QueueHandle_t c1;
    QueueHandle_t c2;
    QueueHandle_t c3;
    PIDState_t pidState;
    bool useExtendedMessages;
    bool depthCompensation;
} PPO2ControlTask_params_t;

void setSetpoint(PPO2_t ppo2)
{
    setpoint = ppo2;
}

PPO2_t getSetpoint(void)
{
    return setpoint;
}

static osThreadId_t *getPPO2_OSThreadId(void)
{
    static osThreadId_t PPO2ControllerTaskHandle;
    return &PPO2ControllerTaskHandle;
}

static osThreadId_t *getSolenoid_OSThreadId(void)
{
    static osThreadId_t SolenoidFireTaskHandle;
    return &SolenoidFireTaskHandle;
}

static PIDNumeric_t *getDutyCyclePtr(void)
{
    static PIDNumeric_t dutyCycle = 0;
    return &dutyCycle;
}

static uint16_t *getAtmoPressurePtr(void)
{
    static uint16_t atmoPressure = 1000;
    return &atmoPressure;
}

uint16_t getAtmoPressure(void)
{
    return *getAtmoPressurePtr();
}

void setAtmoPressure(uint16_t pressure)
{
    *getAtmoPressurePtr() = pressure;
}

static bool *getSolenoidEnablePtr(void)
{
    static bool solenoidEnable = true; /* Activate the solenoid by default to ensure we don't get errors if solenoid control disabled */
    return &solenoidEnable;
}

bool getSolenoidEnable(void)
{
    return *getSolenoidEnablePtr();
}

static PPO2ControlTask_params_t *getControlParams(void)
{
    static PPO2ControlTask_params_t params = {
        .c1 = NULL,
        .c2 = NULL,
        .c3 = NULL,
        .pidState = {
            .derivativeState = 0.0f,
            .integralState = 0.0f,
            .integralMax = 1.0f,
            .integralMin = 0.0f,
            .integralGain = 0.01f,
            .proportionalGain = 1.0f,
            .derivativeGain = 0.0f,
            .saturationCount = 0,
        },
        .useExtendedMessages = false,
        .depthCompensation = true};
    return &params;
}

void setProportionalGain(PIDNumeric_t gain)
{
    getControlParams()->pidState.proportionalGain = gain;
}
void setIntegralGain(PIDNumeric_t gain)
{
    getControlParams()->pidState.integralGain = gain;
}
void setDerivativeGain(PIDNumeric_t gain)
{
    getControlParams()->pidState.derivativeGain = gain;
}

static void PPO2_PIDControlTask(void *arg);
static void PIDSolenoidFireTask(void *arg);
static void MK15SolenoidFireTask(void *arg);

void InitPPO2ControlLoop(QueueHandle_t c1, QueueHandle_t c2, QueueHandle_t c3, bool depthCompensation, bool useExtendedMessages, PPO2ControlScheme_t controlScheme)
{
    PPO2ControlTask_params_t *params = getControlParams();
    params->c1 = c1;
    params->c2 = c2;
    params->c3 = c3;

    params->useExtendedMessages = useExtendedMessages;
    params->depthCompensation = depthCompensation;

    /* Declare this stack externally so we can use it across the different control schemes without impacting our footprint */
    static uint32_t SolenoidFireTask_buffer[SOLENOIDFIRETASK_STACK_SIZE];
    static StaticTask_t SolenoidFireTask_ControlBlock;

    if (controlScheme == PPO2CONTROL_SOLENOID_PID)
    {
        PIDNumeric_t *dutyCycle = getDutyCyclePtr();
        *dutyCycle = 0;

        static uint32_t PPO2_PIDControlTask_buffer[PPO2CONTROLTASK_STACK_SIZE];
        static StaticTask_t PPO2_PIDControlTask_ControlBlock;
        static const osThreadAttr_t PPO2_PIDControlTask_attributes = {
            .name = "PPO2_PIDControlTask",
            .attr_bits = osThreadDetached,
            .cb_mem = &PPO2_PIDControlTask_ControlBlock,
            .cb_size = sizeof(PPO2_PIDControlTask_ControlBlock),
            .stack_mem = &PPO2_PIDControlTask_buffer[0],
            .stack_size = sizeof(PPO2_PIDControlTask_buffer),
            .priority = CAN_PPO2_TX_PRIORITY,
            .tz_module = 0,
            .reserved = 0};

        osThreadId_t *PPO2ControllerTaskHandle = getPPO2_OSThreadId();
        *PPO2ControllerTaskHandle = osThreadNew(PPO2_PIDControlTask, params, &PPO2_PIDControlTask_attributes);

        static const osThreadAttr_t PIDSolenoidFireTask_attributes = {
            .name = "PIDSolenoidFireTask",
            .attr_bits = osThreadDetached,
            .cb_mem = &SolenoidFireTask_ControlBlock,
            .cb_size = sizeof(SolenoidFireTask_ControlBlock),
            .stack_mem = &SolenoidFireTask_buffer[0],
            .stack_size = sizeof(SolenoidFireTask_buffer),
            .priority = CAN_PPO2_TX_PRIORITY,
            .tz_module = 0,
            .reserved = 0};

        osThreadId_t *SolenoidFireTaskHandle = getSolenoid_OSThreadId();
        *SolenoidFireTaskHandle = osThreadNew(PIDSolenoidFireTask, params, &PIDSolenoidFireTask_attributes);
    } else if (controlScheme == PPO2CONTROL_MK15)
    {
        static const osThreadAttr_t PIDSolenoidFireTask_attributes = {
            .name = "PIDSolenoidFireTask",
            .attr_bits = osThreadDetached,
            .cb_mem = &SolenoidFireTask_ControlBlock,
            .cb_size = sizeof(SolenoidFireTask_ControlBlock),
            .stack_mem = &SolenoidFireTask_buffer[0],
            .stack_size = sizeof(SolenoidFireTask_buffer),
            .priority = CAN_PPO2_TX_PRIORITY,
            .tz_module = 0,
            .reserved = 0};

        osThreadId_t *SolenoidFireTaskHandle = getSolenoid_OSThreadId();
        *SolenoidFireTaskHandle = osThreadNew(MK15SolenoidFireTask, params, &PIDSolenoidFireTask_attributes);
    }
    else if(controlScheme == PPO2CONTROL_OFF)
    {
        /* Don't do anything, no PPO2 control requested */
    } else {
        NON_FATAL_ERROR(UNREACHABLE_ERROR);
    }
}

static void MK15SolenoidFireTask(void *arg)
{
    const PPO2ControlTask_params_t *const params = (PPO2ControlTask_params_t *)arg;
    do
    {
        const uint32_t off_time = 6000;   /* Wait for 6 seconds before checking again */
        const uint32_t on_time = 1500;  /* Fire 1.5 seconds to empty the accumulator */

        /* Work out the current PPO2 and the setpoint */
        Consensus_t consensus = peekCellConsensus(params->c1, params->c2, params->c3);
        PIDNumeric_t d_setpoint = (PIDNumeric_t)setpoint / 100.0f;
        PIDNumeric_t measurement = consensus.precisionConsensus;

        /* Check if now is a time when we fire the solenoid */
        if (getSolenoidEnable() && (d_setpoint > measurement))
        {
            setSolenoidOn();
            (void)osDelay(pdMS_TO_TICKS(on_time));
            setSolenoidOff();
        }

        /* Do our off time before waiting again */
        (void)osDelay(pdMS_TO_TICKS(off_time));

    } while (RTOS_LOOP_FOREVER);
}

static void PIDSolenoidFireTask(void *arg)
{
    const PPO2ControlTask_params_t *const params = (PPO2ControlTask_params_t *)arg;
    do
    {
        uint32_t totalFireTime = 5000;   /* Fire for 1000ms */
        uint32_t minimumFireTime = 200;  /* Fire for no less than 100ms */
        uint32_t maximumFireTime = 4900; /* Fire for no longer than 900ms */

        PIDNumeric_t maximumDutyCycle = ((PIDNumeric_t)maximumFireTime) / ((PIDNumeric_t)totalFireTime);
        PIDNumeric_t minimumDutyCycle = ((PIDNumeric_t)minimumFireTime) / ((PIDNumeric_t)totalFireTime);

        PIDNumeric_t dutyCycle = *getDutyCyclePtr();

        /* Establish upper bound on solenoid duty*/
        if (dutyCycle > maximumDutyCycle)
        {
            dutyCycle = maximumDutyCycle;
        }
        /* Establish the lower bound on the solenoid duty, and ensure solenoid is active */
        if ((dutyCycle >= minimumDutyCycle) && getSolenoidEnable())
        {
            /* Do depth compensation by dividing the calculated duty time by the depth in bar*/
            if (params->depthCompensation)
            {
                PIDNumeric_t depthCompCoeff = ((PIDNumeric_t)getAtmoPressure()) / 1000.0f;
                dutyCycle /= depthCompCoeff;

                /* Ensure at deep depths that we don't go smaller than our minimum, which is determined by our solenoid*/
                if (dutyCycle < minimumDutyCycle)
                {
                    dutyCycle = minimumDutyCycle;
                }
            }

            setSolenoidOn();
            (void)osDelay(pdMS_TO_TICKS((PIDNumeric_t)totalFireTime * dutyCycle));
            setSolenoidOff();
            (void)osDelay(pdMS_TO_TICKS((PIDNumeric_t)totalFireTime * (1.0f - dutyCycle)));
        }
        else
        { /* If we don't reach the minimum duty then we just don't fire the solenoid */
            (void)osDelay(pdMS_TO_TICKS(totalFireTime));
        }
    } while (RTOS_LOOP_FOREVER);
}

static PIDNumeric_t updatePID(PIDNumeric_t d_setpoint, PIDNumeric_t measurement, PIDState_t *state)
{
    /* Step PID */
    PIDNumeric_t pTerm = 0;
    PIDNumeric_t iTerm = 0;
    PIDNumeric_t dTerm = 0;
    PIDNumeric_t error = d_setpoint - measurement;

    /* proportional term*/
    pTerm = state->proportionalGain * error;

    /* integral term*/
    state->integralState += state->integralGain * error;

    /* As soon as we are above the setpoint reset the integral so we don't have to wind down*/
    if (error < 0)
    {
        state->integralState = 0;
    }

    if (state->integralState > state->integralMax)
    {
        state->integralState = state->integralMax;
        ++state->saturationCount;
    }
    else if (state->integralState < state->integralMin)
    {
        state->integralState = state->integralMin;
        ++state->saturationCount;
    }
    else
    {
        state->saturationCount = 0; /* We've come out of saturation so reset it */
    }

    iTerm = state->integralState;

    /* derivative term */
    dTerm = state->derivativeGain * (state->derivativeState - measurement);
    state->derivativeState = measurement;

    return pTerm + dTerm + iTerm;
}

static void PPO2_PIDControlTask(void *arg)
{
    PPO2ControlTask_params_t *params = (PPO2ControlTask_params_t *)arg;

    uint32_t PIDPeriod = 100; /* 100ms period */

    do
    {
        Consensus_t consensus = peekCellConsensus(params->c1, params->c2, params->c3);

        /* Transmit Precision PPO2*/
        OxygenCell_t c1 = {0};
        bool c1pick = xQueuePeek(params->c1, &c1, TIMEOUT_100MS_TICKS);
        OxygenCell_t c2 = {0};
        bool c2pick = xQueuePeek(params->c2, &c2, TIMEOUT_100MS_TICKS);
        OxygenCell_t c3 = {0};
        bool c3pick = xQueuePeek(params->c3, &c3, TIMEOUT_100MS_TICKS);

        if (c1pick && c2pick && c3pick && params->useExtendedMessages)
        {
            txPrecisionCells(DIVECAN_SOLO, c1, c2, c3);
        }

        /* It feels like we ought to do something with the cell confidence (go to SP low?) but that implementation is hard so avoid for now
                uint8_t confidence = cellConfidence(consensus);
        */
        PIDNumeric_t d_setpoint = (PIDNumeric_t)setpoint / 100.0f;
        PIDNumeric_t measurement = consensus.precisionConsensus;

        PIDNumeric_t *dutyCycle = getDutyCyclePtr();
        *dutyCycle = updatePID(d_setpoint, measurement, &(params->pidState));

        if (params->useExtendedMessages)
        {
            txPIDState(DIVECAN_SOLO,
                       (params->pidState).proportionalGain,
                       (params->pidState).integralGain,
                       (params->pidState).derivativeGain,
                       (params->pidState).integralState,
                       (params->pidState).derivativeState,
                       *dutyCycle,
                       consensus.precisionConsensus);
        }
        LogPIDState(&(params->pidState), *dutyCycle, d_setpoint);

        (void)osDelay(pdMS_TO_TICKS(PIDPeriod));
    } while (RTOS_LOOP_FOREVER);
}
