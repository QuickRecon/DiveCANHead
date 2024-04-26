#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;

typedef void *TaskHandle_t;

typedef struct
{
    uint32_t data;
} StaticTask_t;

typedef struct
{
    uint32_t test;
} StaticQueue_t;

uint32_t HAL_GetTick();

#ifdef __cplusplus
}
#endif
