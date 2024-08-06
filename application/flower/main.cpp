#include "flower.h"

uint2 remap8x8(uint tid)
{
	return uint2((((tid >> 2) & 0x7) & 0xFFFE) | (tid & 0x1), ((tid >> 1) & 0x3) | (((tid >> 3) & 0x7) & 0xFFFC));
}

uint2 remap16x16(uint tid)
{
	uint2 xy;
	uint2 basic = remap8x8(tid % 64);

	// offset x:   0-63  is 0
	//           64-127  is 8
	//           128-191 is 0
	//           192-255 is 8
	xy.x = basic.x + 8 * ((tid >> 6) % 2);

	// offset y:   0-63  is 0
	//           64-127  is 0
	//           128-191 is 8
	//           192-255 is 8
	xy.y = basic.y + 8 * ((tid >> 7));
	return xy;
}

int main(int argc, const char** argv)
{
	return Flower::get().run(argc, argv);
}