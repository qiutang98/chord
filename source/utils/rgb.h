#pragma once

#include <utils/utils.h>

namespace chord
{
	class RGBA
	{
	public:
		uint8 R = 0;
		uint8 G = 0;
		uint8 B = 0;
		uint8 A = 0;

		RGBA(uint8 RR, uint8 GG, uint8 BB, uint8 AA)
			: R(RR), G(GG), B(BB), A(AA)
		{

		}

		// 
		const uint8* getData() const { return &R; }

		// 
		static const RGBA kBlack;
		static const RGBA kWhite;
		static const RGBA kTransparent;
		static const RGBA kNormal;
	};
}