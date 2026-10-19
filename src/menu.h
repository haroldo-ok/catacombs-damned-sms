#ifndef MENU_H
#define MENU_H

#include "defines.h"

/* The original keeps selection / timer / fizzleFade in a union because only
 * one of them is live in any given state; kept the same way. */
extern u16 menuState;

void Menu_Init(void);
void Menu_Draw(void);
void Menu_Tick(void);
void Menu_TickEnteringLevel(void);
void Menu_DrawEnteringLevel(void);
void Menu_TickGameOver(void);
void Menu_DrawGameOver(void);
void Menu_ResetTimer(void);
void Menu_BeginLoading(void);     /* draw the sweeping bar under the caption */
void Menu_LoadingStep(void);      /* advance it + push it to the VDP         */
void Menu_LoadingPoll(void);      /* same, rate limited to ~15 Hz            */
void Menu_FadeOut(void);

#endif
