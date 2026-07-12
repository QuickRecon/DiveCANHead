/**
 * @file ppo2_autotune_math.h
 * @brief Incremental rebreather plant identification and PID synthesis.
 */
#ifndef PPO2_AUTOTUNE_MATH_H
#define PPO2_AUTOTUNE_MATH_H

#include "ppo2_control_math.h"
#include <stdbool.h>
#include <stdint.h>

/** Local integrating-plus-delay model around one stable operating point. */
typedef struct {
    PIDNumeric_t process_gain;       /**< PPO2 bar/s per unit effective duty */
    PIDNumeric_t dead_time_s;        /**< actuator-to-observation transport delay */
    PIDNumeric_t time_constant_s;    /**< measured mixing/recovery duration */
    PIDNumeric_t fit_rmse_bar;       /**< final-tail noise / residual */
    PIDNumeric_t mixing_excursion_bar; /**< largest rise/fall reversal */
    PIDNumeric_t baseline_ppo2_bar;
    PIDNumeric_t baseline_duty;
    PIDNumeric_t final_ppo2_bar;
    PIDNumeric_t final_duty;
    bool valid;
} AutotunePlantModel_t;

/** Identify rate gain, delay and mixing from an effective-duty pulse/recovery. */
bool autotune_identify_plant(const PIDNumeric_t *duty,
                 const PIDNumeric_t *ppo2_bar, uint16_t n,
                 PIDNumeric_t dt_s, PIDNumeric_t baseline_duty,
                 PIDNumeric_t baseline_ppo2_bar,
                 AutotunePlantModel_t *model);

/** Derive conservative PI gains for the identified integrating delayed plant. */
bool autotune_model_pid(const AutotunePlantModel_t *model,
            PIDNumeric_t controller_dt_s,
            PIDNumeric_t gain_min, PIDNumeric_t gain_max,
            PIDNumeric_t *kp, PIDNumeric_t *ki, PIDNumeric_t *kd);

#endif /* PPO2_AUTOTUNE_MATH_H */
