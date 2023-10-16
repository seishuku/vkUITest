#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "../system/system.h"
#include "../math/math.h"
#include "../vulkan/vulkan.h"
#include "../camera/camera.h"
#include "../font/font.h"
#include "../ui/ui.h"
#include "input.h"

// External data from engine.c
extern uint32_t Width, Height;
extern UI_t UI;

extern uint32_t CursorID;
//////////////////////////////

void Event_KeyDown(void *Arg)
{
	uint32_t *Key=(uint32_t *)Arg;

	switch(*Key)
	{
		default:		break;
	}
}

void Event_KeyUp(void *Arg)
{
	uint32_t *Key=(uint32_t *)Arg;

	switch(*Key)
	{
		default:		break;
	}
}

static uint32_t ActiveID=UINT32_MAX;
static vec2 MousePosition={ 0.0f, 0.0f };

void Event_MouseDown(void *Arg)
{
	MouseEvent_t *MouseEvent=Arg;

	if(MouseEvent->button&MOUSE_BUTTON_LEFT)
		ActiveID=UI_TestHit(&UI, MousePosition);
}

void Event_MouseUp(void *Arg)
{
	MouseEvent_t *MouseEvent=Arg;

	if(MouseEvent->button&MOUSE_BUTTON_LEFT)
		ActiveID=UINT32_MAX;
}

void Event_Mouse(void *Arg)
{
	MouseEvent_t *MouseEvent=Arg;

	// Calculate relative movement
	MousePosition=Vec2_Add(MousePosition, (float)MouseEvent->dx, (float)MouseEvent->dy);

	MousePosition.x=min(max(MousePosition.x, 0.0f), (float)Width);
	MousePosition.y=min(max(MousePosition.y, 0.0f), (float)Height);

	UI_UpdateCursorPosition(&UI, CursorID, MousePosition);

	if(ActiveID!=UINT32_MAX&&MouseEvent->button&MOUSE_BUTTON_LEFT)
		UI_ProcessControl(&UI, ActiveID, MousePosition);
}
