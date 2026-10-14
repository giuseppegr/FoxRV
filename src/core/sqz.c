#include "core/sqz.h"

#include <SDL3/SDL.h>
#include <string.h>

#define LZW_CLEAR 0x100
#define LZW_END 0x101
#define LZW_FIRST 0x102
#define LZW_MAX_CODES 4096
#define SQZ_LZW 0x10
#define ZIV_SCAN_FROM 0x2ED         /* the stub looks for "MZ" from CS:03ED */

/* --- LZW ---------------------------------------------------------------------------------- */

typedef struct {
    uint16_t prefix;            /* code of the string without its last byte */
    uint8_t last, first;
    uint16_t len;
} LzwEntry;

/* Writes the string of `code` at out[0..len) without bounds checks; the caller made room. */
static void lzw_emit(const LzwEntry *table, int code, uint8_t *out)
{
    int len = code < 0x100 ? 1 : table[code].len;
    while (code >= 0x100) {
        out[--len] = table[code].last;
        code = table[code].prefix;
    }
    out[0] = (uint8_t)code;
}

static int lzw_len(const LzwEntry *table, int code) { return code < 0x100 ? 1 : table[code].len; }
static uint8_t lzw_first(const LzwEntry *table, int code) { return code < 0x100 ? (uint8_t)code : table[code].first; }

static bool lzw_decode(const uint8_t *data, size_t size, uint8_t *out, size_t out_len)
{
    static LzwEntry table[LZW_MAX_CODES];
    uint8_t *tmp = SDL_malloc(LZW_MAX_CODES + 1);     /* one string, which may run past out_len */
    if (!tmp)
        return false;
    size_t pos = 0, n = 0;
    uint32_t bitbuf = 0;
    int bits = 0, width = 9, next = LZW_FIRST, prev = -1;
    while (n < out_len) {
        while (bits < width) {
            if (pos >= size)
                goto done;
            bitbuf = bitbuf << 8 | data[pos++];
            bits += 8;
        }
        bits -= width;
        int code = (int)(bitbuf >> bits) & ((1 << width) - 1);
        bitbuf &= (1u << bits) - 1;
        if (code == LZW_END)
            break;
        if (code == LZW_CLEAR) {
            width = 9;
            next = LZW_FIRST;
            prev = -1;
            continue;
        }
        int len;
        if (code < 0x100 || code < next) {
            len = lzw_len(table, code);
            lzw_emit(table, code, tmp);
        } else if (code == next && prev >= 0) {         /* the string being defined (KwKwK) */
            len = lzw_len(table, prev) + 1;
            lzw_emit(table, prev, tmp);
            tmp[len - 1] = lzw_first(table, prev);
        } else {
            SDL_Log("sqz: invalid LZW code %#x", code);
            SDL_free(tmp);
            return false;
        }
        size_t copy = SDL_min((size_t)len, out_len - n);
        memcpy(out + n, tmp, copy);
        n += copy;
        if (prev >= 0 && next < LZW_MAX_CODES) {
            table[next] = (LzwEntry){(uint16_t)prev, tmp[0], lzw_first(table, prev),
                                     (uint16_t)(lzw_len(table, prev) + 1)};
            next++;
            if (next == 1 << width && width < 12)
                width++;
        }
        prev = code;
    }
done:
    SDL_free(tmp);
    if (n != out_len)
        SDL_Log("sqz: LZW gave %zu bytes, header says %zu", n, out_len);
    return n == out_len;
}

/* --- Huffman + RLE ------------------------------------------------------------------------ */

/* Leaf symbols: < 0x100 a byte; 0x100 = repeat the last byte N times, N = the next symbol;
 * 0x101 = the same with N from the next two symbols, high byte first; above 0x101 = repeat
 * (symbol - 0x100) times. */
static bool huffman_decode(const uint8_t *data, size_t size, uint8_t *out, size_t out_len)
{
    if (size < 2)
        return false;
    size_t tree_bytes = data[0] | (size_t)data[1] << 8, nodes = tree_bytes / 2;
    if (2 + tree_bytes > size)
        return false;
    const uint8_t *tree = data + 2;
    size_t n = 0, node = 0;
    int last = 0, state = 0, high = 0;
    for (size_t i = 2 + tree_bytes; i < size; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            size_t k = node / 2 + ((data[i] >> bit) & 1);
            if (k >= nodes)
                return false;
            unsigned value = tree[2 * k] | (unsigned)tree[2 * k + 1] << 8;
            if (!(value & 0x8000)) {
                node = value;
                continue;
            }
            node = 0;
            unsigned sym = value & 0x7FFF;
            size_t repeat = 0;
            if (state == 1) {
                repeat = sym;
                state = 0;
            } else if (state == 2) {
                high = (int)sym;
                state = 3;
            } else if (state == 3) {
                repeat = (size_t)high << 8 | sym;
                state = 0;
            } else if (sym < 0x100) {
                if (n >= out_len)
                    return false;
                out[n++] = (uint8_t)sym;
                last = (int)sym;
            } else if (sym == 0x100) {
                state = 1;
            } else if (sym == 0x101) {
                state = 2;
            } else {
                repeat = sym - 0x100;
            }
            if (repeat) {
                if (repeat > out_len - n)
                    return false;       /* overshoot: a bad decode */
                memset(out + n, last, repeat);
                n += repeat;
            }
            if (n >= out_len)           /* the remaining bits are padding */
                return true;
        }
    }
    return n == out_len;
}

/* --- public ------------------------------------------------------------------------------- */

uint8_t *sqz_decode(const uint8_t *raw, size_t size, size_t *out_len)
{
    if (size < 4)
        return NULL;
    size_t len = (size_t)raw[0] << 16 | (size_t)raw[3] << 8 | raw[2];
    uint8_t *out = SDL_malloc(SDL_max(len, 1));
    if (!out)
        return NULL;
    bool ok = raw[1] == SQZ_LZW ? lzw_decode(raw + 4, size - 4, out, len)
                                : huffman_decode(raw + 4, size - 4, out, len);
    if (!ok) {
        SDL_free(out);
        return NULL;
    }
    if (out_len)
        *out_len = len;
    return out;
}

uint8_t *fox_unpack(const uint8_t *com, size_t size, size_t *exe_len)
{
    size_t m = ZIV_SCAN_FROM;
    while (m + 1 < size && (com[m] != 'M' || com[m + 1] != 'Z'))
        m++;
    if (m + 1 >= size)
        return NULL;
    size_t start = ((m + 0x100 + 2) & ~(size_t)1) - 0x100;   /* COM loads at 0x100, word-aligned */
    if (start >= size)
        return NULL;
    size_t len;
    uint8_t *exe = sqz_decode(com + start, size - start, &len);
    if (!exe)
        return NULL;
    size_t last_page = exe[2] | (size_t)exe[3] << 8, pages = exe[4] | (size_t)exe[5] << 8;
    size_t expect = pages * 512 - (last_page ? 512 - last_page : 0);
    if (len < 0x20 || exe[0] != 'M' || exe[1] != 'Z' || expect != len) {
        SDL_free(exe);
        return NULL;
    }
    if (exe_len)
        *exe_len = len;
    return exe;
}

void planar_to_indexed(const uint8_t *src, int w, int h, uint8_t *dst)
{
    size_t plane = (size_t)w * h / 8;
    memset(dst, 0, (size_t)w * h);
    for (int p = 0; p < 4; p++)
        for (size_t i = 0; i < plane; i++) {
            uint8_t byte = src[p * plane + i];
            for (int b = 0; byte && b < 8; b++)
                if (byte & (0x80 >> b))
                    dst[i * 8 + b] |= (uint8_t)(1 << p);
        }
}
