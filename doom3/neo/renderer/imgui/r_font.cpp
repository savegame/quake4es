/*
===============================================================================

	Noto Sans Bold, embedded

	Compressed with ImGui's binary_to_compressed_c. The launcher writes with
	it and so do the on-screen touch controls; the array is big, so it is
	compiled in here, once, and handed out to both.

===============================================================================
*/

#if defined(_IMGUI) || defined(_AURORA_LAUNCHER)

#include "fonts/NotoSansBold.h"

/*
====================
R_ImGui_NotoSansBold

The compressed font for ImFontAtlas::AddFontFromMemoryCompressedTTF()
====================
*/
const unsigned int *R_ImGui_NotoSansBold(unsigned int *size)
{
	*size = NotoSansBold_compressed_size;
	return NotoSansBold_compressed_data;
}

#endif
