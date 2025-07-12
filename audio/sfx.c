#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "../system/system.h"
#include "../math/math.h"
#include "../audio/audio.h"
#include "sfx.h"

void SFXStreamData(void *buffer, size_t length)
{
	int16_t *bufferPtr=(int16_t *)buffer;

	for(uint32_t i=0;i<length;i++)
	{
		bufferPtr[0]=0;
		bufferPtr[1]=0;
		bufferPtr+=2;
	}
}

void SFX_Init(void)
{
	Audio_SetStreamCallback(1, SFXStreamData);
	Audio_SetStreamVolume(1, 1.0f);
	Audio_StartStream(1);
}

void SFX_Destroy(void)
{
}
