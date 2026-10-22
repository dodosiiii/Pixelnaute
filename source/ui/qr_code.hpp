#pragma once

#include <stdint.h>
#include <string>
#include <vector>

// QR byte-mode, version 5-L. The fixed matrix keeps generation deterministic
// and needs no heap after encoding beyond two small work buffers.
class QrCodeV5
{
public:
	static const int Size = 37;
	static bool encode(const std::string& payload, bool out[Size][Size]);
};
