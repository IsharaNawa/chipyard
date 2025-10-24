/* jpg.h - C-compatible header for JPEG parsing data structures
 *
 * This header replaces the previous C++-style header. It provides
 * plain C structs and a simple dynamic byte-array (replacement for
 * std::vector<unsigned char>) plus init/free helpers.
 */

#ifndef JPG_H
#define JPG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal, guarded types so this header can be used in freestanding
 * environments where the standard headers are not available. Define
 * JPG_PROVIDE_STD_TYPES=0 externally if you prefer to use the
 * platform's <stddef.h>/<stdbool.h>/<stdint.h>. */
#ifndef JPG_PROVIDE_STD_TYPES
#define JPG_PROVIDE_STD_TYPES 1
#endif

#if JPG_PROVIDE_STD_TYPES
/* Provide minimal types unconditionally to avoid pulling in
 * standard headers in bare-metal builds. Guard the typedefs so
 * they can be overridden by defining JPG_PROVIDE_STD_TYPES=0. */
typedef unsigned char byte;
typedef unsigned int uint;
#ifdef __SIZE_TYPE__
typedef __SIZE_TYPE__ size_t;
#else
typedef unsigned long size_t;
#endif
#ifndef __cplusplus
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#endif

/* Marker definitions (byte values) */
#define SOF0  0xC0u
#define SOF1  0xC1u
#define SOF2  0xC2u
#define SOF3  0xC3u
#define SOF5  0xC5u
#define SOF6  0xC6u
#define SOF7  0xC7u
#define SOF9  0xC9u
#define SOF10 0xCAu
#define SOF11 0xCBu
#define SOF13 0xCDu
#define SOF14 0xCEu
#define SOF15 0xCFu

#define DHT   0xC4u
#define JPG   0xC8u
#define DAC   0xCCu

#define RST0  0xD0u
#define RST1  0xD1u
#define RST2  0xD2u
#define RST3  0xD3u
#define RST4  0xD4u
#define RST5  0xD5u
#define RST6  0xD6u
#define RST7  0xD7u

#define SOI   0xD8u
#define EOI   0xD9u
#define SOS   0xDAu
#define DQT   0xDBu
#define DNL   0xDCu
#define DRI   0xDDu
#define DHP   0xDEu
#define EXP   0xDFu

#define APP0  0xE0u
#define APP1  0xE1u
#define APP2  0xE2u
#define APP3  0xE3u
#define APP4  0xE4u
#define APP5  0xE5u
#define APP6  0xE6u
#define APP7  0xE7u
#define APP8  0xE8u
#define APP9  0xE9u
#define APP10 0xEAu
#define APP11 0xEBu
#define APP12 0xECu
#define APP13 0xEDu
#define APP14 0xEEu
#define APP15 0xEFu

#define JPG0  0xF0u
#define JPG1  0xF1u
#define JPG2  0xF2u
#define JPG3  0xF3u
#define JPG4  0xF4u
#define JPG5  0xF5u
#define JPG6  0xF6u
#define JPG7  0xF7u
#define JPG8  0xF8u
#define JPG9  0xF9u
#define JPG10 0xFAu
#define JPG11 0xFBu
#define JPG12 0xFCu
#define JPG13 0xFDu
#define COM   0xFEu
#define TEM   0x01u

typedef struct {
    uint table[64];
    bool set;
} QuantizationTable;

typedef struct {
    byte offsets[17];
    byte symbols[162];
    bool set;
} HuffmanTable;

typedef struct {
    byte horizontalSamplingFactor;
    byte verticalSamplingFactor;
    byte quantizationTableID;
    byte huffmanDCTableID;
    byte huffmanACTableID;
    bool used;
} ColorComponent;

/* Simple dynamic array of bytes (replacement for std::vector<byte>) */
typedef struct {
    /* Static inline storage to avoid dynamic allocation on bare-metal. */
    /* Capacity can be tuned. 64KiB is more than enough for embedded huffman data in test images. */
#ifndef JPG_STATIC_HUFFMAN_CAPACITY
#define JPG_STATIC_HUFFMAN_CAPACITY 30720
#endif
    byte data[JPG_STATIC_HUFFMAN_CAPACITY];
    size_t size;
    size_t capacity; /* remains for compatibility; will be set to JPG_STATIC_HUFFMAN_CAPACITY */
} ByteArray;

/* API for ByteArray */
void byte_array_init(ByteArray *a);
int  byte_array_push(ByteArray *a, byte b); /* returns 0 on success, -1 on OOM */
void byte_array_free(ByteArray *a);

typedef struct {
    QuantizationTable quantizationTables[4];
    HuffmanTable huffmanDCTables[4];
    HuffmanTable huffmanACTables[4];

    byte frameType;
    uint height;
    uint width;
    byte numComponents;
    bool zeroBased;

    byte startOfSelection;
    byte endOfSelection;
    byte successiveApproximationHigh;
    byte successiveApproximationLow;

    byte restartInterval;

    ColorComponent colorComponents[3];

    /* Per-header reader state to allow re-entrant parsing from different data sources. */
    const unsigned char *reader_data;
    size_t reader_size;
    size_t reader_pos;

    ByteArray huffmanData;

    bool valid;
} Header;

/* Helpers for Header lifecycle */
void header_init(Header *h);
void header_free(Header *h);

static const byte zigZagMap[64] = {
    0,   1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

#ifdef __cplusplus
}
#endif

#endif /* JPG_H */