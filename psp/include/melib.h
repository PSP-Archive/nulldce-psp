#pragma once

#ifdef __cplusplus  
extern "C" {
#endif 

#include <time.h>
#include <stdbool.h>
#include <pspkerneltypes.h>
#include <psptypes.h>
#include <driver/me.h>

typedef int (*fun)(int ptr);

void ME_Init();
void ME_End(); 

void InitFunction(fun _fun, int arg);
void StartFunction();

void MeDcacheWritebackInvalidateAll();

struct me_struct* mei;

#ifdef __cplusplus  
}
#endif 