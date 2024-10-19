#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif


#define configSUPPORT_STATIC_ALLOCATION 1
#define pdFALSE			( ( BaseType_t ) 0 )
#define pdTRUE			( ( BaseType_t ) 1 )

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

#define pdMS_TO_TICKS(x) x

#ifdef __cplusplus
}
#endif
