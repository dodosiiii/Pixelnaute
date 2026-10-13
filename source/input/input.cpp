#include "input.hpp"

//--------------------------------------------------------------------------
// Suivi des controles : boutons, Circle Pad, C-Stick et geste tactile.
//--------------------------------------------------------------------------
void inputPoll(InputState& in)
{
	static bool wasTouching = false;
	static int lastPx = 0, lastPy = 0;
	static float smoothVY = 0.0f;

	hidScanInput();
	if (irrstSharedMem)
	{
		irrstScanInput();
	}

	in.down = hidKeysDown();
	in.held = hidKeysHeld();
	in.up = hidKeysUp();
	in.touch = (in.held & KEY_TOUCH) != 0;
	in.touchDown = (in.down & KEY_TOUCH) != 0;
	in.touchUp = (in.up & KEY_TOUCH) != 0;
	hidTouchRead(&in.tp);

	// Lecture Circle Pad (joystick principal)
	circlePosition cpad;
	hidCircleRead(&cpad);
	in.circleX = cpad.dx;
	in.circleY = cpad.dy;

	// Lecture C-Stick (petit joystick droit New 3DS)
	if (irrstSharedMem)
	{
		circlePosition cstick;
		irrstCstickRead(&cstick);
		in.cstickX = cstick.dx;
		in.cstickY = cstick.dy;
	}
	else
	{
		in.cstickX = 0;
		in.cstickY = 0;
	}

	in.dragX = 0;
	in.dragY = 0;
	in.velocityY = 0.0f;

	if (in.touchDown)
	{
		lastPx = in.tp.px;
		lastPy = in.tp.py;
		smoothVY = 0.0f;
	}
	else if (in.touch && wasTouching)
	{
		in.dragX = in.tp.px - lastPx;
		in.dragY = in.tp.py - lastPy;
		lastPx = in.tp.px;
		lastPy = in.tp.py;
		smoothVY = smoothVY * 0.6f + (float)in.dragY * 0.4f;
	}
	else if (!in.touch)
	{
		smoothVY = 0.0f;
	}

	wasTouching = in.touch;
	in.velocityY = smoothVY;
}

bool inputTopTouch(const InputState& in, int& x, int& y)
{
	if (!in.touch) return false;
	if (in.tp.px >= 320)
	{
		x = in.tp.px;
		y = in.tp.py;
		return true;
	}
	return false;
}

bool inputBottomTouch(const InputState& in, int& x, int& y)
{
	if (!in.touch) return false;
	if (in.tp.px < 320)
	{
		x = in.tp.px;
		y = in.tp.py;
		return true;
	}
	return false;
}