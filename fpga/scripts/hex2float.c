#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [-b|-l] HEX\n"
        "  HEX : 8 hex digits (e.g. 40b00000) or 0x40b00000\n"
        "  -b  : interpret HEX as big-endian byte order (default)\n"
        "  -l  : interpret HEX as little-endian byte order\n",
        prog);
}

int main(int argc, char **argv) {
    int opt_be = 1; /* default big-endian */
    int argi = 1;
    if (argc < 2) { usage(argv[0]); return 1; }

    if (argv[1][0] == '-') {
        if (strcmp(argv[1], "-b") == 0) { opt_be = 1; argi = 2; }
        else if (strcmp(argv[1], "-l") == 0) { opt_be = 0; argi = 2; }
        else { usage(argv[0]); return 1; }
    }

    if (argi >= argc) { usage(argv[0]); return 1; }

    const char *hex = argv[argi];
    if (strncmp(hex, "0x", 2) == 0 || strncmp(hex, "0X", 2) == 0) hex += 2;

    if (strlen(hex) != 8) {
        fprintf(stderr, "Error: HEX must be 8 hex digits, got '%s'\n", hex);
        return 2;
    }

    char *endptr = NULL;
    errno = 0;
    unsigned long val = strtoul(hex, &endptr, 16);
    if (errno != 0 || endptr == NULL || *endptr != '\0') {
        fprintf(stderr, "Error parsing hex '%s'\n", hex);
        return 3;
    }
    uint32_t word = (uint32_t)val;

    uint8_t bytes[4];
    if (opt_be) {
        /* big-endian: hex string is MSB first */
        bytes[0] = (word >> 24) & 0xFF;
        bytes[1] = (word >> 16) & 0xFF;
        bytes[2] = (word >> 8) & 0xFF;
        bytes[3] = (word >> 0) & 0xFF;
    } else {
        /* little-endian: hex string is LSB first */
        bytes[3] = (word >> 24) & 0xFF;
        bytes[2] = (word >> 16) & 0xFF;
        bytes[1] = (word >> 8) & 0xFF;
        bytes[0] = (word >> 0) & 0xFF;
    }

    /* Recreate uint32 in host byte order from bytes[] then reinterpret as float */
    uint32_t host_word = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | ((uint32_t)bytes[3] << 0);

    union { uint32_t u; float f; } conv;
    conv.u = host_word;

    /* Print both the integer hex and the float value */
    printf("hex=0x%08x\n", word);
    printf("interpreted (host bits) = 0x%08x\n", conv.u);
    printf("float = %.9g\n", conv.f);

    return 0;
}
