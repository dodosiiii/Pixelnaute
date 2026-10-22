#include "qr_code.hpp"
#include <algorithm>
#include <cstdlib>
#include <string.h>

namespace
{
constexpr int kDataCodewords = 108;
constexpr int kEccCodewords = 26;
constexpr int kTotalCodewords = kDataCodewords + kEccCodewords;
bool modules[QrCodeV5::Size][QrCodeV5::Size];
bool functionModules[QrCodeV5::Size][QrCodeV5::Size];

static uint8_t gfMultiply(uint8_t x, uint8_t y)
{
	uint8_t result = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (y & 1) result ^= x;
		const bool high = (x & 0x80) != 0;
		x <<= 1;
		if (high) x ^= 0x1D;
		y >>= 1;
	}
	return result;
}

static void setFunction(int x, int y, bool value)
{
	if (x < 0 || y < 0 || x >= QrCodeV5::Size || y >= QrCodeV5::Size) return;
	modules[y][x] = value;
	functionModules[y][x] = true;
}

static void drawFinder(int centerX, int centerY)
{
	for (int dy = -1; dy <= 7; ++dy)
		for (int dx = -1; dx <= 7; ++dx)
		{
			const int x = centerX + dx, y = centerY + dy;
			const bool inside = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;
			const bool dark = inside && (dx == 0 || dx == 6 || dy == 0 || dy == 6 ||
				(dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
			setFunction(x, y, dark);
		}
}

static void drawFormat()
{
	// Error correction L = 01, mask pattern 0. BCH(15,5) then fixed XOR.
	const int data = (1 << 3) | 0;
	int rem = data << 10;
	for (int i = 14; i >= 10; --i)
		if ((rem & (1 << i)) != 0) rem ^= 0x537 << (i - 10);
	const int bits = ((data << 10) | rem) ^ 0x5412;
	for (int i = 0; i <= 5; ++i) setFunction(8, i, ((bits >> i) & 1) != 0);
	setFunction(8, 7, ((bits >> 6) & 1) != 0);
	setFunction(8, 8, ((bits >> 7) & 1) != 0);
	setFunction(7, 8, ((bits >> 8) & 1) != 0);
	for (int i = 9; i < 15; ++i) setFunction(14 - i, 8, ((bits >> i) & 1) != 0);
	for (int i = 0; i < 8; ++i)
		setFunction(QrCodeV5::Size - 1 - i, 8, ((bits >> i) & 1) != 0);
	for (int i = 8; i < 15; ++i)
		setFunction(8, QrCodeV5::Size - 15 + i, ((bits >> i) & 1) != 0);
	setFunction(8, QrCodeV5::Size - 8, true);
}

static void makeFunctionPatterns()
{
	memset(modules, 0, sizeof(modules));
	memset(functionModules, 0, sizeof(functionModules));
	drawFinder(0, 0);
	drawFinder(QrCodeV5::Size - 7, 0);
	drawFinder(0, QrCodeV5::Size - 7);
	for (int i = 8; i < QrCodeV5::Size - 8; ++i)
	{
		if (!functionModules[6][i]) setFunction(i, 6, (i & 1) == 0);
		if (!functionModules[i][6]) setFunction(6, i, (i & 1) == 0);
	}
	// Version 5 has one alignment pattern at (30,30).
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx)
			setFunction(30 + dx, 30 + dy,
				std::max(abs(dx), abs(dy)) != 1);
	drawFormat();
}

}

bool QrCodeV5::encode(const std::string& payload, bool out[Size][Size])
{
	if (payload.empty() || payload.size() > 106) return false;
	uint8_t bits[kDataCodewords * 8] = {};
	size_t bitCount = 0;
	// Byte mode, 8-bit character count for versions 1 through 9.
	for (int i = 3; i >= 0; --i) bits[bitCount++] = (uint8_t)((4 >> i) & 1);
	const uint32_t length = (uint32_t)payload.size();
	for (int i = 7; i >= 0; --i) bits[bitCount++] = (uint8_t)((length >> i) & 1);
	for (unsigned char c : payload)
		for (int i = 7; i >= 0; --i) bits[bitCount++] = (uint8_t)((c >> i) & 1);
	const size_t capacityBits = (size_t)kDataCodewords * 8;
	const size_t terminator = std::min((size_t)4, capacityBits - bitCount);
	bitCount += terminator; // bits are pre-zeroed
	while (bitCount % 8) ++bitCount;
	uint8_t data[kDataCodewords] = {};
	for (size_t i = 0; i < bitCount; ++i)
		data[i / 8] |= (uint8_t)(bits[i] << (7 - i % 8));
	for (size_t i = bitCount / 8, pad = 0; i < kDataCodewords; ++i, ++pad)
		data[i] = (pad & 1) ? 0x11 : 0xEC;

	uint8_t generator[kEccCodewords + 1] = {};
	uint8_t next[kEccCodewords + 1] = {};
	size_t generatorSize = 1;
	generator[0] = 1;
	uint8_t root = 1;
	for (int i = 0; i < kEccCodewords; ++i)
	{
		memset(next, 0, sizeof(next));
		for (size_t j = 0; j < generatorSize; ++j)
		{
			next[j] ^= generator[j];
			next[j + 1] ^= gfMultiply(generator[j], root);
		}
		memcpy(generator, next, sizeof(generator));
		++generatorSize;
		root = gfMultiply(root, 2);
	}
	uint8_t ecc[kEccCodewords] = {};
	for (uint8_t byte : data)
	{
		const uint8_t factor = byte ^ ecc[0];
		for (int i = 0; i < kEccCodewords - 1; ++i)
			ecc[i] = ecc[i + 1] ^ gfMultiply(generator[i + 1], factor);
		ecc[kEccCodewords - 1] = gfMultiply(generator[kEccCodewords], factor);
	}
	uint8_t codewords[kTotalCodewords];
	memcpy(codewords, data, sizeof(data));
	memcpy(codewords + kDataCodewords, ecc, sizeof(ecc));

	makeFunctionPatterns();
	int bitIndex = 0;
	bool upward = true;
	for (int right = Size - 1; right >= 1; right -= 2)
	{
		if (right == 6) --right;
		for (int vertical = 0; vertical < Size; ++vertical)
		{
			const int y = upward ? Size - 1 - vertical : vertical;
			for (int j = 0; j < 2; ++j)
			{
				const int x = right - j;
				if (functionModules[y][x]) continue;
				bool dark = false;
				if (bitIndex < kTotalCodewords * 8)
					dark = ((codewords[bitIndex / 8] >> (7 - bitIndex % 8)) & 1) != 0;
				++bitIndex;
				if ((x + y) % 2 == 0) dark = !dark; // mask 0
				modules[y][x] = dark;
			}
		}
		upward = !upward;
	}
	for (int y = 0; y < Size; ++y)
		for (int x = 0; x < Size; ++x) out[y][x] = modules[y][x];
	return bitIndex >= kTotalCodewords * 8;
}
