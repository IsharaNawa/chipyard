/* jpg.c - implementations for the C-compatible jpg helper types
 */

#include "jpg.h"
#include <stdlib.h>
#include <string.h>

void byte_array_init(ByteArray *a) {
    if (!a) return;
    a->data = NULL;
    a->size = 0;
    a->capacity = 0;
}

int byte_array_push(ByteArray *a, byte b) {
    if (!a) return -1;
    if (a->size + 1 > a->capacity) {
        size_t new_cap = a->capacity ? a->capacity * 2 : 64;
        byte *p = (byte*)realloc(a->data, new_cap);
        if (!p) return -1;
        a->data = p;
        a->capacity = new_cap;
    }
    a->data[a->size++] = b;
    return 0;
}

void byte_array_free(ByteArray *a) {
    if (!a) return;
    free(a->data);
    a->data = NULL;
    a->size = 0;
    a->capacity = 0;
}

void header_init(Header *h) {
    if (!h) return;
    memset(h, 0, sizeof(*h));
    for (int i = 0; i < 4; ++i) {
        h->quantizationTables[i].set = false;
        h->huffmanDCTables[i].set = false;
        h->huffmanACTables[i].set = false;
    }
    for (int i = 0; i < 3; ++i) {
        h->colorComponents[i].horizontalSamplingFactor = 1;
        h->colorComponents[i].verticalSamplingFactor = 1;
        h->colorComponents[i].quantizationTableID = 0;
        h->colorComponents[i].huffmanDCTableID = 0;
        h->colorComponents[i].huffmanACTableID = 0;
        h->colorComponents[i].used = false;
    }
    byte_array_init(&h->huffmanData);
    h->valid = true;
}

void header_free(Header *h) {
    if (!h) return;
    byte_array_free(&h->huffmanData);
}
