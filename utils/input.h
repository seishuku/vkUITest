#ifndef __INPUT_H__
#define __INPUT_H__

typedef enum
{
	KB_UNKNOWN=0,
	KB_SPACE=32,
	KB_APOSTROPHE=39,
	KB_COMMA=44,
	KB_MINUS=45,
	KB_PERIOD=46,
	KB_SLASH=47,
	KB_ZERO=48,
	KB_ONE=49,
	KB_TWO=50,
	KB_THREE=51,
	KB_FOUR=52,
	KB_FIVE=53,
	KB_SIX=54,
	KB_SEVEN=55,
	KB_EIGHT=56,
	KB_NINE=57,
	KB_SEMICOLON=59,
	KB_EQUAL=61,

	KB_A=65,
	KB_B=66,
	KB_C=67,
	KB_D=68,
	KB_E=69,
	KB_F=70,
	KB_G=71,
	KB_H=72,
	KB_I=73,
	KB_J=74,
	KB_K=75,
	KB_L=76,
	KB_M=77,
	KB_N=78,
	KB_O=79,
	KB_P=80,
	KB_Q=81,
	KB_R=82,
	KB_S=83,
	KB_T=84,
	KB_U=85,
	KB_V=86,
	KB_W=87,
	KB_X=88,
	KB_Y=89,
	KB_Z=90,

	KB_LEFT_BRACKET=91,
	KB_BACKSLASH=92,
	KB_RIGHT_BRACKET=93,
	KB_GRAVE_ACCENT=96,
	KB_WORLD_1=161,
	KB_WORLD_2=162,
	KB_ESCAPE=256,
	KB_ENTER=257,
	KB_TAB=258,
	KB_BACKSPACE=259,
	KB_INSERT=260,
	KB_DEL=261,
	KB_RIGHT=262,
	KB_LEFT=263,
	KB_DOWN=264,
	KB_UP=265,
	KB_PAGE_UP=266,
	KB_PAGE_DOWN=267,
	KB_HOME=268,
	KB_END=269,
	KB_CAPS_LOCK=280,
	KB_SCROLL_LOCK=281,
	KB_NUM_LOCK=282,
	KB_PRINT_SCREEN=283,
	KB_PAUSE=284,

	KB_F1=290,
	KB_F2=291,
	KB_F3=292,
	KB_F4=293,
	KB_F5=294,
	KB_F6=295,
	KB_F7=296,
	KB_F8=297,
	KB_F9=298,
	KB_F10=299,
	KB_F11=300,
	KB_F12=301,

	KB_NP_0=320,
	KB_NP_1=321,
	KB_NP_2=322,
	KB_NP_3=323,
	KB_NP_4=324,
	KB_NP_5=325,
	KB_NP_6=326,
	KB_NP_7=327,
	KB_NP_8=328,
	KB_NP_9=329,
	KB_NP_DECIMAL=330,
	KB_NP_DIVIDE=331,
	KB_NP_MULTIPLY=332,
	KB_NP_SUBTRACT=333,
	KB_NP_ADD=334,
	KB_NP_ENTER=335,
	KB_NP_EQUAL=336,

	KB_LSHIFT=340,
	KB_LCTRL=341,
	KB_LALT=342,
	KB_LSUPER=343,
	KB_RSHIFT=344,
	KB_RCTRL=345,
	KB_RALT=346,
	KB_RSUPER=347,
	KB_MENU=348,
} Keycodes_t;

typedef enum
{
	MOUSE_BUTTON_1=0x00000001,
	MOUSE_BUTTON_2=0x00000002,
	MOUSE_BUTTON_3=0x00000004,
	MOUSE_BUTTON_4=0x00000008,
	MOUSE_BUTTON_5=0x00000010,
	MOUSE_BUTTON_LEFT=MOUSE_BUTTON_1,
	MOUSE_BUTTON_RIGHT=MOUSE_BUTTON_2,
	MOUSE_BUTTON_MIDDLE=MOUSE_BUTTON_3,
} Mousecodes_t;

typedef struct
{
	int32_t dx, dy, dz;
	uint32_t button;
} MouseEvent_t;

void Event_KeyDown(void *Arg);
void Event_KeyUp(void *Arg);
void Event_MouseDown(void *Arg);
void Event_MouseUp(void *Arg);
void Event_Mouse(void *Arg);

#endif
