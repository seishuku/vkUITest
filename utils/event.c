#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "../system/system.h"
#include "../math/math.h"
#include "../vulkan/vulkan.h"
#include "../camera/camera.h"
#include "../physics/physics.h"
#include "../physics/particle.h"
#include "../audio/audio.h"
#include "../audio/sounds.h"
#include "../ui/ui.h"
#include "../font/font.h"
#include "../console/console.h"
#include "event.h"

extern UI_t UI;
extern uint32_t cursorID;

static uint32_t activeID=UINT32_MAX;
static vec2 mousePosition={ 0.0f, 0.0f };

#ifdef ANDROID
static vec2 oldMousePosition={ 0.0f, 0.0f };
#endif

bool Event_Trigger(EventID ID, void *arg)
{
	if(ID<0||ID>=NUM_EVENTS)
		return false;

	MouseEvent_t *mouseEvent=(MouseEvent_t *)arg;
	uint32_t key=*((uint32_t *)arg);

	switch(ID)
	{
		case EVENT_KEYDOWN:
		{
			UI_Control_t *control=UI_FindControlByID(&UI, activeID);

			if(activeID!=UINT32_MAX&&control->type==UI_CONTROL_EDITTEXT)
			{
				switch(key)
				{
					case KB_LEFT:
						UI_EditTextMoveCursor(&UI, activeID, -1);
						break;

					case KB_RIGHT:
						UI_EditTextMoveCursor(&UI, activeID, 1);
						break;

					case KB_BACKSPACE:
						UI_EditTextBackspace(&UI, activeID);
						break;

					case KB_DEL:
						UI_EditTextDelete(&UI, activeID);
						break;

					default:
						if(key>=32&&key<=126)
							UI_EditTextInsertChar(&UI, activeID, tolower(key));
						break;
				}

				return true;
			}

			switch(key)
			{
				default:		break;
			}

			break;
		}

		case EVENT_KEYUP:
		{
			switch(key)
			{
				default:		break;
			}

			break;
		}

		case EVENT_MOUSEDOWN:
		{
#ifndef ANDROID
			if(mouseEvent->button&MOUSE_BUTTON_LEFT)
				activeID=UI_TestHit(&UI, mousePosition);
#else
			mousePosition.x=(float)mouseEvent->dx;
			mousePosition.y=(float)mouseEvent->dy;

			if(mouseEvent->button&MOUSE_TOUCH)
			{
				activeID=UI_TestHit(&UI, mousePosition);

				if(activeID!=UINT32_MAX)
					UI_ProcessControl(&UI, activeID, mousePosition);
			}
#endif
			break;
		}

		case EVENT_MOUSEUP:
		{
#ifndef ANDROID
			if(mouseEvent->button&MOUSE_BUTTON_LEFT)
				activeID=UINT32_MAX;
#else
			mousePosition.x=(float)mouseEvent->dx;
			mousePosition.y=(float)mouseEvent->dy;

			if(mouseEvent->button&MOUSE_TOUCH)
				activeID=UINT32_MAX;
#endif
			break;
		}

		case EVENT_MOUSEMOVE:
		{
#ifndef ANDROID
			// Calculate relative movement
			mousePosition=Vec2_Add(mousePosition, (float)mouseEvent->dx, (float)mouseEvent->dy);

			mousePosition.x=clampf(mousePosition.x, 0.0f, (float)config.renderWidth);
			mousePosition.y=clampf(mousePosition.y, 0.0f, (float)config.renderHeight);

			UI_UpdateCursorPosition(&UI, cursorID, mousePosition);

			if(activeID!=UINT32_MAX&&mouseEvent->button&MOUSE_BUTTON_LEFT)
				UI_ProcessControl(&UI, activeID, mousePosition);
			else if(mouseEvent->button&MOUSE_BUTTON_LEFT)
			{
			}
#else
			oldMousePosition=mousePosition;
			mousePosition.x=(float)mouseEvent->dx;
			mousePosition.y=(float)mouseEvent->dy;

			if(activeID==UINT32_MAX&&mouseEvent->button&MOUSE_TOUCH)
			{
			}
#endif
			break;
		}

		default:
			return false;
	}

	return true;
}
