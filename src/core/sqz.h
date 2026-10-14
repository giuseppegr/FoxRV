/* The game's compression and graphics formats (documentation/OVERVIEW.md §2, §3, §5).
 *
 *  - SQZ: a 4-byte header (out_len = b0<<16 | b3<<8 | b2, type = b1), then LZW for type
 *    0x10 (MSB-first codes of 9..12 bits, 0x100 clear, 0x101 end), otherwise a Huffman tree
 *    with run-length codes.
 *  - FOX.COM is packed with ZIVPack: after an "MZ" marker comes an ordinary SQZ-LZW stream
 *    that decodes to the real MZ executable.
 *  - Pictures, tiles, sprites and glyphs are 4 sequential bit-planes (plane 0 = bit 0).
 */
#ifndef FOXRV_SQZ_H
#define FOXRV_SQZ_H

#include <stddef.h>
#include <stdint.h>

/* Decoded data (free with SDL_free), or NULL if `raw` is not a valid SQZ stream. */
uint8_t *sqz_decode(const uint8_t *raw, size_t size, size_t *out_len);

/* The MZ executable inside a ZIVPack-ed FOX.COM (free with SDL_free), or NULL. */
uint8_t *fox_unpack(const uint8_t *com, size_t size, size_t *exe_len);

/* 4 planes of w*h/8 bytes each -> w*h colour indices 0..15. */
void planar_to_indexed(const uint8_t *src, int w, int h, uint8_t *dst);

#endif
