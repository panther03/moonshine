#include <zlib.h>

/* Avoid zlib's 8 KiB BYFOUR table; launcher payloads are small and one-shot. */
static const uLong kCrcTable[16] = {
	0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
	0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
	0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
	0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL,
};

uLong ZEXPORT crc32(uLong crc, const Bytef *buf, uInt len)
{
	if (buf == Z_NULL)
		return 0;

	crc ^= 0xFFFFFFFFUL;
	while (len-- != 0)
	{
		crc = (crc >> 4) ^ kCrcTable[(crc ^ *buf) & 0x0FUL];
		crc = (crc >> 4) ^ kCrcTable[(crc ^ (*buf >> 4)) & 0x0FUL];
		++buf;
	}
	return crc ^ 0xFFFFFFFFUL;
}
