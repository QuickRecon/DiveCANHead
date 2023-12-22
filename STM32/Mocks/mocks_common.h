#pragma once

#include <stdint.h>
#include <stddef.h>

typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef long TickType_t;

typedef void *TaskHandle_t;

typedef struct
{
    int data;
} StaticTask_t;

typedef struct
{
    int test;
} StaticQueue_t;

uint32_t HAL_GetTick();