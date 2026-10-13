#pragma once
//=============================================================================
// input.hpp - Etat des controles (boutons, touch, circle pad, C-Stick New 3DS)
//=============================================================================
#include "../common.hpp"
#include <3ds.h>

struct InputState
{
	u32 down = 0;     // boutons presses ce frame
	u32 held = 0;     // boutons maintenus
	u32 up = 0;       // boutons relaches ce frame
	bool touch = false;
	touchPosition tp{};   // position (ecran bas 320x240 ou ecran haut 400x240)
	bool touchDown = false;  // toucher initie ce frame
	bool touchUp = false;    // toucher relache ce frame

	// Circle Pad (joystick principal) : composantes ~ -156 .. +156
	s16 circleX = 0;
	s16 circleY = 0;

	// C-Stick (petit joystick droit exclusif New 3DS) : ~ -156 .. +156
	s16 cstickX = 0;
	s16 cstickY = 0;

	//--- suivi du geste (pour le defilement au doigt) ---
	int  dragX = 0;      // deplacement depuis l'image precedente
	int  dragY = 0;
	float velocityY = 0.0f;  // vitesse verticale lissee, en pixels par image
};

// Lit l'etat des controles. A appeler une fois par frame.
void inputPoll(InputState& in);

// Position touche sur l'ecran haut (0..399, 0..239) si touch >= 0
bool inputTopTouch(const InputState& in, int& x, int& y);

// Position touche sur l'ecran bas (0..319, 0..239)
bool inputBottomTouch(const InputState& in, int& x, int& y);