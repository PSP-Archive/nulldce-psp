#pragma once
//bleh stupid windoze header
#include "plugins/plugin_header.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define verify(x) if((x)==false){ printf("Verify Failed  : " #x "\n in %s -> %s : %d \n",__FUNCTION__,__FILE__,__LINE__); for(;;);}
#define TCHAR char

void libGDR_ReadSubChannel(u8 * buff, u32 format, u32 len);
u32 libGDR_GetTrackNumber(u32 sector, u32& elapsed);