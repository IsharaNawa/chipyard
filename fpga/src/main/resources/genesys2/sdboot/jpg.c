/* genesys2_code/jpg.c
 * Definition of the shared static MCU buffer used by the genesys2 decoder.
 * Placing the array here avoids multiple definitions across translation units
 * and keeps dynamic allocation out of the bare-metal build.
 */

#include "jpg.h"

// #include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Define the shared MCU buffer with the capacity controlled by the
 * GENESYS2_MAX_MCUS macro (default provided in jpg.h). */
MCU genesys2_mcus[GENESYS2_MAX_MCUS];
/* jpg.c - implementations for the C-compatible jpg helper types
 */

#include "jpg.h"

void byte_array_init(ByteArray *a) {
    if (!a) return;
    a->size = 0;
    a->capacity = JPG_STATIC_HUFFMAN_CAPACITY;
}

int byte_array_push(ByteArray *a, byte b) {
    if (!a) return -1;
    if (a->size + 1 > a->capacity) {
        /* Out of static buffer space */
        return -1;
    }
    a->data[a->size++] = b;
    return 0;
}

void byte_array_free(ByteArray *a) {
    if (!a) return;
    /* No-op for static buffer */
    a->size = 0;
}

void header_init(Header *h) {

    if (!h) return;

    /* Initialize only scalar fields to avoid writing large static buffers
     * (e.g., ByteArray.data) which may live in DDR before it's initialized.
     * This prevents faults during very early boot when DDR isn't ready. */

    for (int i = 0; i < 4; ++i) {
        /* mark tables as not set */
        h->quantizationTables[i].set = false;
        h->huffmanDCTables[i].set = false;
        h->huffmanACTables[i].set = false;
    }

    h->frameType = 0;
    h->height = 0;
    h->width = 0;
    h->numComponents = 0;
    h->zeroBased = false;

    h->startOfSelection = 0;
    h->endOfSelection = 0;
    h->successiveApproximationHigh = 0;
    h->successiveApproximationLow = 0;

    h->restartInterval = 0;

    for (int i = 0; i < 3; ++i) {
        h->colorComponents[i].horizontalSamplingFactor = 1;
        h->colorComponents[i].verticalSamplingFactor = 1;
        h->colorComponents[i].quantizationTableID = 0;
        h->colorComponents[i].huffmanDCTableID = 0;
        h->colorComponents[i].huffmanACTableID = 0;
        h->colorComponents[i].used = false;
    }

    /* Initialize reader state to null/zero without touching h->huffmanData.data */

    h->reader_data = (const unsigned char*)0;
    h->reader_size = 0;
    h->reader_pos = 0;

    /* Initialize ByteArray metadata; leave the large static buffer untouched */
    byte_array_init(&h->huffmanData);
    h->valid = true;
}

void header_free(Header *h) {
    if (!h) return;
    byte_array_free(&h->huffmanData);
}

/* IDCT constants and accessors ------------------------------------------------
 * Compute once and expose pointers for decoder use. This keeps runtime
 * overhead small on bare-metal and centralizes math usage in one place.
 */
// static float idct_m_arr[6]; /* m0, m1, m3, m5, m2, m4 */
// static float idct_s_arr[8]; /* s0..s7 */
// static int   idct_inited = 0;

// void idct_init(void) {
//     if (idct_inited) return;
//     idct_inited = 1;
//     idct_m_arr[0] = 2.0f * cosf(1.0f / 16.0f * 2.0f * M_PI); /* m0 */
//     idct_m_arr[1] = 2.0f * cosf(2.0f / 16.0f * 2.0f * M_PI); /* m1 */
//     idct_m_arr[2] = 2.0f * cosf(2.0f / 16.0f * 2.0f * M_PI); /* m3 */
//     idct_m_arr[3] = 2.0f * cosf(3.0f / 16.0f * 2.0f * M_PI); /* m5 */
//     idct_m_arr[4] = idct_m_arr[0] - idct_m_arr[3]; /* m2 = m0 - m5 */
//     idct_m_arr[5] = idct_m_arr[0] + idct_m_arr[3]; /* m4 = m0 + m5 */

//     idct_s_arr[0] = cosf(0.0f / 16.0f * M_PI) / sqrtf(8.0f);
//     idct_s_arr[1] = cosf(1.0f / 16.0f * M_PI) / 2.0f;
//     idct_s_arr[2] = cosf(2.0f / 16.0f * M_PI) / 2.0f;
//     idct_s_arr[3] = cosf(3.0f / 16.0f * M_PI) / 2.0f;
//     idct_s_arr[4] = cosf(4.0f / 16.0f * M_PI) / 2.0f;
//     idct_s_arr[5] = cosf(5.0f / 16.0f * M_PI) / 2.0f;
//     idct_s_arr[6] = cosf(6.0f / 16.0f * M_PI) / 2.0f;
//     idct_s_arr[7] = cosf(7.0f / 16.0f * M_PI) / 2.0f;
// }

// const float* idct_get_m(void) { idct_init(); return idct_m_arr; }
// const float* idct_get_s(void) { idct_init(); return idct_s_arr; }
