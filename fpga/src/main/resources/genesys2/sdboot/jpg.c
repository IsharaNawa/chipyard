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
