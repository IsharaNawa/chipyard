#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* decoder.c - C port of decoder.cpp (uses jpg.h C API)
 *
 * This file implements JPEG header parsing functions using C file I/O
 * and the Header/ByteArray types from jpg.h.
 */

#include "jpg.h"
#include "embedded_cropped_cat.h"

// Use below on bear metal : comment below and add includes for platform, uart and kprintln implementations
#include "uart.h"
#include "kprintf.h"
#include "platform.h"

#define read_csr(reg) ({ unsigned long __tmp; \
  asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
  __tmp; })


// Use below on host machine , spike
// #include <stdio.h>
// #define kprintf(...) printf(__VA_ARGS__)
// #define kputc(c) putchar(c)
// #define kprintln(fmt, ...) do { printf(fmt, ##__VA_ARGS__); putchar('\n'); } while (0)
// #define uart_init() ((void)0)




/////////////////////////////////////////
// Start : JPEG File Reading Stage
/////////////////////////////////////////

/* Helper: read a single byte from the memory stream, set header->valid=false on EOF */
static int read_byte_or_fail(Header *header) {
	if (!header || header->reader_data == (const unsigned char*)0 || header->reader_pos >= header->reader_size) {
		if (header) header->valid = false;
		return -1;
	}
	return header->reader_data[header->reader_pos++] & 0xFF;
}

/* SOF specifies frame type, dimensions, and number of color components */
void readStartOfFrame(Header* header) {
	if (header->numComponents != 0) {
		kprintln("Error - Multiple SOFs detected");
		header->valid = false;
		return;
	}

	int hi = read_byte_or_fail(header);
	if (!header->valid) return;
	int lo = read_byte_or_fail(header);
	if (!header->valid) return;
	uint length = ((uint)hi << 8) + (uint)lo;

	int precision = read_byte_or_fail(header);
	if (!header->valid) return;
		if (precision != 8) {
			kprintln("Error - Invalid precision: %d", (int)precision);
		header->valid = false;
		return;
	}

	int hhi = read_byte_or_fail(header); if (!header->valid) return;
	int hlo = read_byte_or_fail(header); if (!header->valid) return;
	header->height = ((uint)hhi << 8) + (uint)hlo;
	int whi = read_byte_or_fail(header); if (!header->valid) return;
	int wlo = read_byte_or_fail(header); if (!header->valid) return;
	header->width = ((uint)whi << 8) + (uint)wlo;
	if (header->height == 0 || header->width == 0) {
		kprintln("Error - Invalid dimensions");
		header->valid = false;
		return;
	}

	int numComp = read_byte_or_fail(header); if (!header->valid) return;
	header->numComponents = (byte)numComp;
		if (header->numComponents == 4) {
			kprintln("Error - CMYK color mode not supported");
		header->valid = false;
		return;
	}
	if (header->numComponents == 0) {
			kprintln("Error - Number of color components must not be zero");
		header->valid = false;
		return;
	}

	for (uint i = 0; i < header->numComponents; ++i) {
	int componentID = read_byte_or_fail(header); if (!header->valid) return;
		if (componentID == 0) {
			header->zeroBased = true;
		}
		if (header->zeroBased) {
			componentID += 1;
		}
		if (componentID == 4 || componentID == 5) {
			kprintln("Error - YIQ color mode not supported");
			header->valid = false;
			return;
		}
		if (componentID == 0 || componentID > 3) {
			kprintln("Error - Invalid component ID: %d", (int)componentID);
			header->valid = false;
			return;
		}
		ColorComponent* component = &header->colorComponents[componentID - 1];
		if (component->used) {
			kprintln("Error - Duplicate color component ID");
			header->valid = false;
			return;
		}
		component->used = true;
	int samplingFactor = read_byte_or_fail(header); if (!header->valid) return;
		component->horizontalSamplingFactor = (byte)(samplingFactor >> 4);
		component->verticalSamplingFactor = (byte)(samplingFactor & 0x0F);
	int qid = read_byte_or_fail(header); if (!header->valid) return;
		component->quantizationTableID = (byte)qid;
		if (component->quantizationTableID > 3) {
			kprintln("Error - Invalid quantization table ID in frame components");
			header->valid = false;
			return;
		}
	}
	if (length - 8 - (3 * header->numComponents) != 0) {
		kprintln("Error - SOF invalid");
		header->valid = false;
	}
}

/* DQT contains one or more quantization tables */
void readQuantizationTable(Header* header) {
	int hi = read_byte_or_fail(header); if (!header->valid) return;
	int lo = read_byte_or_fail(header); if (!header->valid) return;
	int length = ((int)hi << 8) + lo;
	length -= 2;

	while (length > 0) {
	int tableInfo = read_byte_or_fail(header); if (!header->valid) return;
		length -= 1;
		int tableID = tableInfo & 0x0F;

		if (tableID > 3) {
			kprintln("Error - Invalid quantization table ID: %d", (int)tableID);
			header->valid = false;
			return;
		}
		header->quantizationTables[tableID].set = true;

		if (tableInfo >> 4 != 0) {
			for (uint i = 0; i < 64; ++i) {
				int a = read_byte_or_fail(header); if (!header->valid) return;
				int b = read_byte_or_fail(header); if (!header->valid) return;
				header->quantizationTables[tableID].table[zigZagMap[i]] = ((uint)a << 8) + (uint)b;
			}
			length -= 128;
		}
		else {
			for (uint i = 0; i < 64; ++i) {
				int v = read_byte_or_fail(header); if (!header->valid) return;
				header->quantizationTables[tableID].table[zigZagMap[i]] = (uint)v;
			}
			length -= 64;
		}
	}

	if (length != 0) {
		kprintln("Error - DQT invalid");
		header->valid = false;
	}
}

/* DHT contains one or more Huffman tables */
void readHuffmanTable(Header* header) {
	int hi = read_byte_or_fail(header); if (!header->valid) return;
	int lo = read_byte_or_fail(header); if (!header->valid) return;
	int length = ((int)hi << 8) + lo;
	length -= 2;

	while (length > 0) {
		int tableInfo = read_byte_or_fail(header); if (!header->valid) return;
		int tableID = tableInfo & 0x0F;
		int ACTable = (tableInfo >> 4) & 0x01;

		if (tableID > 3) {
			kprintln("Error - Invalid Huffman table ID: %d", (int)tableID);
			header->valid = false;
			return;
		}

		HuffmanTable* hTable;
		if (ACTable) {
			hTable = &header->huffmanACTables[tableID];
		}
		else {
			hTable = &header->huffmanDCTables[tableID];
		}
		hTable->set = true;

		hTable->offsets[0] = 0;
		uint allSymbols = 0;
		for (uint i = 1; i <= 16; ++i) {
			int cnt = read_byte_or_fail(header); if (!header->valid) return;
			allSymbols += (uint)cnt;
			hTable->offsets[i] = (byte)allSymbols;
		}
		if (allSymbols > 162) {
			kprintln("Error - Too many symbols in Huffman table");
			header->valid = false;
			return;
		}

		for (uint i = 0; i < allSymbols; ++i) {
			int sym = read_byte_or_fail(header); if (!header->valid) return;
			hTable->symbols[i] = (byte)sym;
		}

		length -= 17 + (int)allSymbols;
	}
	if (length != 0) {
		kprintln("Error - DHT invalid");
		header->valid = false;
	}
}

/* SOS contains color component info for the next scan */
void readStartOfScan(Header* header) {
	if (header->numComponents == 0) {
		kprintln("Error - SOS detected before SOF");
		header->valid = false;
		return;
	}

	int hi = read_byte_or_fail(header); if (!header->valid) return;
	int lo = read_byte_or_fail(header); if (!header->valid) return;
	uint length = ((uint)hi << 8) + (uint)lo;

	for (uint i = 0; i < header->numComponents; ++i) {
		header->colorComponents[i].used = false;
	}

	int numComponents = read_byte_or_fail(header); if (!header->valid) return;
	for (int i = 0; i < numComponents; ++i) {
		int componentID = read_byte_or_fail(header); if (!header->valid) return;
			if (header->zeroBased) componentID += 1;
			if (componentID > header->numComponents) {
				kprintln("Error - Invalid color component ID: %d", (int)componentID);
			header->valid = false;
			return;
		}
		ColorComponent* component = &header->colorComponents[componentID - 1];
			if (component->used) {
				kprintln("Error - Duplicate color component ID: %d", (int)componentID);
			header->valid = false;
			return;
		}
		component->used = true;

	int huffmanTableIDs = read_byte_or_fail(header); if (!header->valid) return;
		component->huffmanDCTableID = (byte)(huffmanTableIDs >> 4);
		component->huffmanACTableID = (byte)(huffmanTableIDs & 0x0F);
			if (component->huffmanDCTableID > 3) {
				kprintln("Error - Invalid Huffman DC table ID: %d", (int)component->huffmanDCTableID);
			header->valid = false;
			return;
		}
		if (component->huffmanACTableID > 3) {
				kprintln("Error - Invalid Huffman AC table ID: %d", (int)component->huffmanACTableID);
			header->valid = false;
			return;
		}
	}

	int s1 = read_byte_or_fail(header); if (!header->valid) return;
	int s2 = read_byte_or_fail(header); if (!header->valid) return;
	header->startOfSelection = (byte)s1;
	header->endOfSelection = (byte)s2;
	int sa = read_byte_or_fail(header); if (!header->valid) return;
	header->successiveApproximationHigh = (byte)(sa >> 4);
	header->successiveApproximationLow = (byte)(sa & 0x0F);

	if (header->startOfSelection != 0 || header->endOfSelection != 63) {
		kprintln("Error - Invalid spectral selection");
		header->valid = false;
		return;
	}
	if (header->successiveApproximationHigh != 0 || header->successiveApproximationLow != 0) {
		kprintln("Error - Invalid successive approximation");
		header->valid = false;
		return;
	}

	if (length - 6 - (2 * numComponents) != 0) {
		kprintln("Error - SOS invalid");
		header->valid = false;
	}
}

/* restart interval is needed to stay synchronized during data scans */
void readRestartInterval(Header* header) {
	int hi = read_byte_or_fail(header); if (!header->valid) return;
	int lo = read_byte_or_fail(header); if (!header->valid) return;
	uint length = ((uint)hi << 8) + (uint)lo;

	int a = read_byte_or_fail(header); if (!header->valid) return;
	int b = read_byte_or_fail(header); if (!header->valid) return;
	header->restartInterval = (byte)(((uint)a << 8) + (uint)b);
	if (length - 4 != 0) {
		kprintln("Error - DRI invalid");
		header->valid = false;
	}
}

/* APPNs simply get skipped based on length */
void readAPPN(Header* header) {
	int hi = read_byte_or_fail(header); if (!header->valid) return;
	int lo = read_byte_or_fail(header); if (!header->valid) return;
	uint length = ((uint)hi << 8) + (uint)lo;

	for (uint i = 0; i < length - 2; ++i) {
		if (read_byte_or_fail(header) < 0) return;
	}
}

/* comments simply get skipped based on length */
void readComment(Header* header) {
	int hi = read_byte_or_fail(header); if (!header->valid) return;
	int lo = read_byte_or_fail(header); if (!header->valid) return;
	uint length = ((uint)hi << 8) + (uint)lo;

	for (uint i = 0; i < length - 2; ++i) {
		if (read_byte_or_fail(header) < 0) return;
	}
}

/* Initialize per-header in-memory reader using the embedded image bytes. */
/* removed memreader_init: readJPG will initialize per-header reader state after header_init */
Header* readJPG(Header* header, const unsigned char *data, size_t size) {

	/* Caller provides storage for Header to allow re-entrant usage and avoid function-static state. */
	if (!header) return (Header*)0;
	byte last = 0, current = 0;

	header_init(header);

	/* Initialize per-header reader */
	header->reader_data = data;
	header->reader_size = size;
	header->reader_pos = 0;

	/* first two bytes must be 0xFF, SOI */
	int v = read_byte_or_fail(header); if (!header->valid) { return header; }
	last = (byte)v;
	v = read_byte_or_fail(header); if (!header->valid) { return header; }
	current = (byte)v;
	if (last != 0xFF || current != SOI) {
		header->valid = false;
		return header;
	}
	v = read_byte_or_fail(header); if (!header->valid) return header; last = (byte)v;
	v = read_byte_or_fail(header); if (!header->valid) return header; current = (byte)v;

	while (header->valid) {
		if (last != 0xFF) {
			kprintln("Error - Expected a marker");
			header->valid = false;
			return header;
		}

		if (current == SOF0) {
			header->frameType = SOF0;
			readStartOfFrame(header);
		}
		else if (current == DQT) {
			readQuantizationTable(header);
		}
		else if (current == DHT) {
			readHuffmanTable(header);
		}
		else if (current == SOS) {
			readStartOfScan(header);
			break;
		}
		else if (current == DRI) {
			readRestartInterval(header);
		}
		else if (current >= APP0 && current <= APP15) {
			readAPPN(header);
		}
		else if (current == COM) {
			readComment(header);
		}
		else if ((current >= JPG0 && current <= JPG13) || current == DNL || current == DHP || current == EXP) {
			readComment(header);
		}
		else if (current == TEM) {
			/* TEM has no size */
		}
		else if (current == 0xFF) {
			int nv = read_byte_or_fail(header); if (!header->valid) return header; current = (byte)nv;
			continue;
		}
		else if (current == SOI) {
			kprintln("Error - Embedded JPGs not supported");
			header->valid = false;
			return header;
		}
		else if (current == EOI) {
			kprintln("Error - EOI detected before SOS");
			header->valid = false;
			return header;
		}
		else if (current == DAC) {
			kprintln("Error - Arithmetic Coding mode not supported");
			header->valid = false;
			return header;
		}
		else if (current >= SOF0 && current <= SOF15) {
			kprintln("Error - SOF marker not supported: 0x%x", (unsigned)current);
			header->valid = false;
			return header;
		}
		else if (current >= RST0 && current <= RST7) {
			kprintln("Error - RSTN detected before SOS");
			header->valid = false;
			return header;
		}
		else {
			/* skip unknown/unsupported marker */
			int hi = read_byte_or_fail(header); if (!header->valid) break;
			int lo = read_byte_or_fail(header); if (!header->valid) break;
			int length = ((int)hi << 8) + lo;
			length -= 2;
			while (length--) {
				(void)read_byte_or_fail(header);
				if (!header->valid) break;
			}
		}
		int nv = read_byte_or_fail(header); if (!header->valid) return header; last = (byte)nv;
		nv = read_byte_or_fail(header); if (!header->valid) return header; current = (byte)nv;
	}

	if (header->valid) {
		int cv = read_byte_or_fail(header); if (!header->valid) return header; current = (byte)cv;
		while (1) {
			last = current;
			int nv = read_byte_or_fail(header); if (!header->valid) return header; current = (byte)nv;
			if (last == 0xFF) {
				if (current == EOI) {
					break;
				} else if (current == 0x00) {
					if (byte_array_push(&header->huffmanData, last) != 0) { header->valid = false; return header; }
					nv = read_byte_or_fail(header); if (!header->valid) return header; current = (byte)nv;
				} else if (current >= RST0 && current <= RST7) {
					nv = read_byte_or_fail(header); if (!header->valid) return header; current = (byte)nv;
				} else if (current == 0xFF) {
					continue;
				} else {
					kprintln("Error - Invalid marker during compressed data scan: 0x%x", (unsigned)current);
					header->valid = false;
					return header;
				}
			} else {
				if (byte_array_push(&header->huffmanData, last) != 0) { header->valid = false; return header; }
			}
		}
	}

	if (header->numComponents != 1 && header->numComponents != 3) {
		kprintln("Error - %d color components given (1 or 3 required)", (int)header->numComponents);
		header->valid = false;
		return header;
	}

	if (header->colorComponents[0].horizontalSamplingFactor != 1 || header->colorComponents[0].verticalSamplingFactor != 1) {
		kprintln("Error - Unsupported sampling factor");
		header->valid = false;
		return header;
	}
	if (header->numComponents == 3) {
		if (header->colorComponents[1].horizontalSamplingFactor != 1 || header->colorComponents[1].verticalSamplingFactor != 1) {
			kprintln("Error - Unsupported sampling factor");
			header->valid = false;
			return header;
		}
		if (header->colorComponents[2].horizontalSamplingFactor != 1 || header->colorComponents[2].verticalSamplingFactor != 1) {
			kprintln("Error - Unsupported sampling factor");
			header->valid = false;
			return header;
		}
	}
	for (uint i = 0; i < header->numComponents; ++i) {
		if (header->quantizationTables[header->colorComponents[i].quantizationTableID].set == false) {
			kprintln("Error - Color component using uninitialized quantization table");
			header->valid = false;
			return header;
		}
		if (header->huffmanDCTables[header->colorComponents[i].huffmanDCTableID].set == false) {
			kprintln("Error - Color component using uninitialized Huffman DC table");
			header->valid = false;
			return header;
		}
		if (header->huffmanACTables[header->colorComponents[i].huffmanACTableID].set == false) {
			kprintln("Error - Color component using uninitialized Huffman AC table");
			header->valid = false;
			return header;
		}
	}

	return header;
}
/////////////////////////////////////////
// End : JPEG File Reading Stage
/////////////////////////////////////////



/////////////////////////////////////////
// Start : Huffman Decoding Stage (genesys2/bare-metal friendly)
/////////////////////////////////////////

/* Forward prototypes for new functions */

/* Generate canonical codes for a Huffman table into the provided codes[] array. */
static void generateCodesLocal(const HuffmanTable* hTable, uint* codes) {
	unsigned int code = 0;
	for (uint i = 0; i < 16; ++i) {
		for (uint j = hTable->offsets[i]; j < hTable->offsets[i + 1]; ++j) {
			codes[j] = code;
			code += 1;
		}
		code <<= 1;
	}
}

typedef struct {
	ByteArray* data;
	size_t nextByte;
	unsigned nextBit;
} BitReader;

static void br_init(BitReader* br, ByteArray* data) {
	br->data = data;
	br->nextByte = 0;
	br->nextBit = 0;
}

static int br_readBit(BitReader* br) {
	if (br->nextByte >= br->data->size) return -1;
	int bit = (br->data->data[br->nextByte] >> (7 - br->nextBit)) & 1;
	br->nextBit += 1;
	if (br->nextBit == 8) {
		br->nextBit = 0;
		br->nextByte += 1;
	}
	return bit;
}

static int br_readBits(BitReader* br, const uint length) {
	int bits = 0;
	for (uint i = 0; i < length; ++i) {
		int bit = br_readBit(br);
		if (bit == -1) { bits = -1; break; }
		bits = (bits << 1) | bit;
	}
	return bits;
}

static void br_align(BitReader* br) {
	if (br->nextByte >= br->data->size) return;
	if (br->nextBit != 0) {
		br->nextBit = 0;
		br->nextByte += 1;
	}
}

/* Return symbol (0..255) or -1 on error */
/* Get next Huffman symbol using a precomputed codes[] table. */
static int getNextSymbol(BitReader* b, const HuffmanTable* hTable, const uint* codes) {
	unsigned int currentCode = 0;
	for (uint i = 0; i < 16; ++i) {
		int bit = br_readBit(b);
		if (bit == -1) return -1;
		currentCode = (currentCode << 1) | (unsigned)bit;

		for (uint j = hTable->offsets[i]; j < hTable->offsets[i + 1]; ++j) {
			if (codes[j] == currentCode) {
				return (int)hTable->symbols[j];
			}
		}
	}
	return -1;
}

static bool decodeMCUComponent(BitReader* b, int* component, int* previousDC,
						const HuffmanTable* dcTable, const uint* dcCodes,
						const HuffmanTable* acTable, const uint* acCodes) {
	int lenSym = getNextSymbol(b, dcTable, dcCodes);
	if (lenSym == -1) {
		kprintln("Error - Invalid DC value");
		return false;
	}
	if (lenSym > 11) {
		kprintln("Error - DC coefficient length greater than 11");
		return false;
	}
	int coeff = 0;
	if (lenSym != 0) {
		coeff = br_readBits(b, (uint)lenSym);
		if (coeff == -1) { kprintln("Error - Invalid DC value"); return false; }
		if (coeff < (1 << (lenSym - 1))) coeff -= (1 << lenSym) - 1;
	}
	component[0] = coeff + *previousDC;
	*previousDC = component[0];

	uint i = 1;
	while (i < 64) {
	int sym = getNextSymbol(b, acTable, acCodes);
		if (sym == -1) { kprintln("Error - Invalid AC value"); return false; }
		if (sym == 0x00) {
			for (; i < 64; ++i) component[zigZagMap[i]] = 0;
			return true;
		}
		unsigned numZeroes = (unsigned)(sym >> 4);
		unsigned coeffLength = (unsigned)(sym & 0x0F);
		if (sym == 0xF0) numZeroes = 16;
		if (i + numZeroes >= 64) { kprintln("Error - Zero run-length exceeded MCU"); return false; }
		for (unsigned j = 0; j < numZeroes; ++j, ++i) component[zigZagMap[i]] = 0;
		if (coeffLength > 10) { kprintln("Error - AC coefficient length greater than 10"); return false; }
		if (coeffLength != 0) {
			int v = br_readBits(b, coeffLength);
			if (v == -1) { kprintln("Error - Invalid AC value"); return false; }
			if (v < (1 << (coeffLength - 1))) v -= (1 << coeffLength) - 1;
			component[zigZagMap[i]] = v;
			i += 1;
		}
	}
	return true;
}

/* Helper to get pointer to component array inside MCU by index */
static int* mcu_channel_ptr(MCU* mcus, uint mcuIndex, uint comp) {
	MCU* m = &mcus[mcuIndex];
	switch (comp) {
		case 0: return m->y;
		case 1: return m->cb;
		case 2: return m->cr;
		default: return 0;
	}
}

/* The shared MCU buffer `genesys2_mcus` is defined in genesys2_code/jpg.c
 * and declared in genesys2_code/jpg.h so the decoder can use it without
 * allocating memory at runtime. */

MCU* decodeHuffmanData(Header* const header) {
	const uint mcuWidth = (header->width + 7) / 8;
	const uint mcuHeight = (header->height + 7) / 8;
	size_t total = (size_t)mcuHeight * (size_t)mcuWidth;
	if (total == 0) return 0;
	if (total > GENESYS2_MAX_MCUS) {
		kprintln("Error - MCU count %zu exceeds static capacity %d", total, GENESYS2_MAX_MCUS);
		return 0;
	}

	/* Precompute canonical codes for each Huffman table into local arrays. */
	uint dcCodes[4][162];
	uint acCodes[4][162];
	for (uint i = 0; i < 4; ++i) {
		/* Initialize to a sentinel (0xFFFFFFFF) for safety.
		 * Use volatile pointer stores in a simple loop so the compiler
		 * will not substitute a call to memset/memcpy in optimized builds. */
		{
			volatile uint *p = (volatile uint *)dcCodes[i];
			for (uint k = 0; k < 162; ++k) p[k] = 0xFFFFFFFFu;
		}
		{
			volatile uint *p = (volatile uint *)acCodes[i];
			for (uint k = 0; k < 162; ++k) p[k] = 0xFFFFFFFFu;
		}
		if (header->huffmanDCTables[i].set) generateCodesLocal(&header->huffmanDCTables[i], dcCodes[i]);
		if (header->huffmanACTables[i].set) generateCodesLocal(&header->huffmanACTables[i], acCodes[i]);
	}

	BitReader br;
	br_init(&br, &header->huffmanData);

	int previousDC[3] = {0,0,0};

	for (size_t idx = 0; idx < total; ++idx) {
		if (header->restartInterval != 0 && idx % header->restartInterval == 0) {
			previousDC[0] = previousDC[1] = previousDC[2] = 0;
			br_align(&br);
		}
		for (uint c = 0; c < header->numComponents; ++c) {
			int* compPtr = mcu_channel_ptr(genesys2_mcus, (uint)idx, c);
			if (compPtr == 0) { return 0; }
			uint dcid = header->colorComponents[c].huffmanDCTableID;
			uint acid = header->colorComponents[c].huffmanACTableID;
			if (!decodeMCUComponent(&br, compPtr, &previousDC[c],
				&header->huffmanDCTables[dcid], dcCodes[dcid],
				&header->huffmanACTables[acid], acCodes[acid])) {
				return 0;
			}
		}
	}

		return genesys2_mcus;
}

/////////////////////////////////////////
// End : Huffman Decoding Stage
/////////////////////////////////////////

/////////////////////////////////////////
// Start : Dequantization Stage (genesys2/bare-metal friendly)
/////////////////////////////////////////

/* multiply each coefficient in a component by the corresponding
	* quantization table value (both are stored in natural order)
	*/
static void dequantizeMCUComponent(const QuantizationTable* qTable, int* component) {
	for (uint i = 0; i < 64; ++i) {
		component[i] *= (int)qTable->table[i];
	}
}

/* dequantize all MCUs using the caller-provided/static buffer
	* Use 0 instead of NULL for bare-metal friendliness.
	*/
static void dequantize(const Header* header, MCU* mcus) {
	const uint mcuHeight = (header->height + 7) / 8;
	const uint mcuWidth = (header->width + 7) / 8;

	size_t total = (size_t)mcuHeight * (size_t)mcuWidth;
	for (size_t i = 0; i < total; ++i) {
		for (uint j = 0; j < header->numComponents; ++j) {
			int* comp = mcu_channel_ptr(mcus, (uint)i, j);
			if (comp != 0) {
				dequantizeMCUComponent(&header->quantizationTables[header->colorComponents[j].quantizationTableID], comp);
			}
		}
	}
}

/////////////////////////////////////////
// End : Dequantization Stage
/////////////////////////////////////////

/////////////////////////////////////////
// Start : Inverse DCT Stage (genesys2)
/////////////////////////////////////////

/* Optimized inverse DCT component (port of c_version implementation).
 * Uses the precomputed IDCT constants exposed by jpg.c (idct_get_m/idct_get_s).
 * Operates in-place on a single 8x8 block (component array of 64 ints).
 */
static void inverseDCTComponent(int* component) {
	// Correct JPEG IDCT constants - testing with -G0 to disable sdata optimization
	
	const float m0 = 1.847759065f;  // 2*cos(1*pi/16) - NOT USED but declared
	const float m1 = 1.847759065f;  // 2*cos(1*pi/16)
	const float m3 = -1.961570560f; // -2*cos(3*pi/16)
	const float m5 = 1.414213562f;  // sqrt(2)
	const float m2 = 1.082392200f;  // 2*cos(3*pi/16)  
	const float m4 = -2.613125930f; // -2*cos(1*pi/16) - sqrt(2)

	const float s0 = 0.353553391f;  // 1/(2*sqrt(2))
	const float s1 = 0.490392640f;  // cos(3*pi/8) / sqrt(2)
	const float s2 = 0.461939766f;  // cos(2*pi/8) / sqrt(2)  
	const float s3 = 0.415734806f;  // cos(1*pi/8) / sqrt(2)
	const float s4 = 0.353553391f;  // 1/(2*sqrt(2))
	const float s5 = 0.490392640f;  // cos(3*pi/8) / sqrt(2)
	const float s6 = 0.461939766f;  // cos(2*pi/8) / sqrt(2)
	const float s7 = 0.415734806f;  // cos(1*pi/8) / sqrt(2)

	float intermediate[64];

	/* process columns */
	for (uint i = 0; i < 8; ++i) {
		const float g0 = component[0 * 8 + i] * s0;
		const float g1 = component[4 * 8 + i] * s4;
		const float g2 = component[2 * 8 + i] * s2;
		const float g3 = component[6 * 8 + i] * s6;
		const float g4 = component[5 * 8 + i] * s5;
		const float g5 = component[1 * 8 + i] * s1;
		const float g6 = component[7 * 8 + i] * s7;
		const float g7 = component[3 * 8 + i] * s3;

		const float f0 = g0;
		const float f1 = g1;
		const float f2 = g2;
		const float f3 = g3;
		const float f4 = g4 - g7;
		const float f5 = g5 + g6;
		const float f6 = g5 - g6;
		const float f7 = g4 + g7;

		const float e0 = f0;
		const float e1 = f1;
		const float e2 = f2 - f3;
		const float e3 = f2 + f3;
		const float e4 = f4;
		const float e5 = f5 - f7;
		const float e6 = f6;
		const float e7 = f5 + f7;
		const float e8 = f4 + f6;

		const float d0 = e0;
		const float d1 = e1;
		const float d2 = e2 * m1;
		const float d3 = e3;
		const float d4 = e4 * m2;
		const float d5 = e5 * m3;
		const float d6 = e6 * m4;
		const float d7 = e7;
		const float d8 = e8 * m5;

		const float c0 = d0 + d1;
		const float c1 = d0 - d1;
		const float c2 = d2 - d3;
		const float c3 = d3;
		const float c4 = d4 + d8;
		const float c5 = d5 + d7;
		const float c6 = d6 - d8;
		const float c7 = d7;
		const float c8 = c5 - c6;

		const float b0 = c0 + c3;
		const float b1 = c1 + c2;
		const float b2 = c1 - c2;
		const float b3 = c0 - c3;
		const float b4 = c4 - c8;
		const float b5 = c8;
		const float b6 = c6 - c7;
		const float b7 = c7;

		intermediate[0 * 8 + i] = b0 + b7;
		intermediate[1 * 8 + i] = b1 + b6;
		intermediate[2 * 8 + i] = b2 + b5;
		intermediate[3 * 8 + i] = b3 + b4;
		intermediate[4 * 8 + i] = b3 - b4;
		intermediate[5 * 8 + i] = b2 - b5;
		intermediate[6 * 8 + i] = b1 - b6;
		intermediate[7 * 8 + i] = b0 - b7;
	}

	/* process rows */
	for (uint i = 0; i < 8; ++i) {
		const float g0 = intermediate[i * 8 + 0] * s0;
		const float g1 = intermediate[i * 8 + 4] * s4;
		const float g2 = intermediate[i * 8 + 2] * s2;
		const float g3 = intermediate[i * 8 + 6] * s6;
		const float g4 = intermediate[i * 8 + 5] * s5;
		const float g5 = intermediate[i * 8 + 1] * s1;
		const float g6 = intermediate[i * 8 + 7] * s7;
		const float g7 = intermediate[i * 8 + 3] * s3;

		const float f0 = g0;
		const float f1 = g1;
		const float f2 = g2;
		const float f3 = g3;
		const float f4 = g4 - g7;
		const float f5 = g5 + g6;
		const float f6 = g5 - g6;
		const float f7 = g4 + g7;

		const float e0 = f0;
		const float e1 = f1;
		const float e2 = f2 - f3;
		const float e3 = f2 + f3;
		const float e4 = f4;
		const float e5 = f5 - f7;
		const float e6 = f6;
		const float e7 = f5 + f7;
		const float e8 = f4 + f6;

		const float d0 = e0;
		const float d1 = e1;
		const float d2 = e2 * m1;
		const float d3 = e3;
		const float d4 = e4 * m2;
		const float d5 = e5 * m3;
		const float d6 = e6 * m4;
		const float d7 = e7;
		const float d8 = e8 * m5;

		const float c0 = d0 + d1;
		const float c1 = d0 - d1;
		const float c2 = d2 - d3;
		const float c3 = d3;
		const float c4 = d4 + d8;
		const float c5 = d5 + d7;
		const float c6 = d6 - d8;
		const float c7 = d7;
		const float c8 = c5 - c6;

		const float b0 = c0 + c3;
		const float b1 = c1 + c2;
		const float b2 = c1 - c2;
		const float b3 = c0 - c3;
		const float b4 = c4 - c8;
		const float b5 = c8;
		const float b6 = c6 - c7;
		const float b7 = c7;

		// kprintln("processing");

		component[i * 8 + 0] = (int)(b0 + b7 + 0.5f);
		component[i * 8 + 1] = (int)(b1 + b6 + 0.5f);
		component[i * 8 + 2] = (int)(b2 + b5 + 0.5f);
		component[i * 8 + 3] = (int)(b3 + b4 + 0.5f);
		component[i * 8 + 4] = (int)(b3 - b4 + 0.5f);
		component[i * 8 + 5] = (int)(b2 - b5 + 0.5f);
		component[i * 8 + 6] = (int)(b1 - b6 + 0.5f);
		component[i * 8 + 7] = (int)(b0 - b7 + 0.5f);
	}
}

/* Perform inverse DCT on all MCUs using the optimized component function */
static void inverseDCT(const Header* header, MCU* mcus) {
	const uint mcuHeight = (header->height + 7) / 8;
	const uint mcuWidth = (header->width + 7) / 8;
	size_t total = (size_t)mcuHeight * (size_t)mcuWidth;
	for (size_t i = 0; i < total; ++i) {
		for (uint j = 0; j < header->numComponents; ++j) {
			int* comp = mcu_channel_ptr(mcus, (uint)i, j);
			if (comp != 0) inverseDCTComponent(comp);
		}
	}
}

/////////////////////////////////////////
// End : Inverse DCT Stage
/////////////////////////////////////////


/////////////////////////////////////////
// Start : Debugging / BMP Output Stage
/////////////////////////////////////////

/* Small helpers: print a single hex nibble and a two-digit hex byte using kputc */
static inline char _hex_digit(unsigned v) {
	return (v < 10) ? ('0' + v) : ('a' + (v - 10));
}

static void print_hex2(unsigned v) {
	unsigned byte = v & 0xFFu;
	kputc(_hex_digit((byte >> 4) & 0xF));
	kputc(_hex_digit(byte & 0xF));
}

/* Print the BMP bytes as two-digit hex to the console (stdout) using kprintf/kputc.
 * Diagnostics continue to be printed with kprintln (goes to stderr or host stdout depending on mapping).
 */
static void printBMP(const Header* header, const MCU* mcus) {
	if (!header || !mcus) return;
	const uint mcuHeight = (header->height + 7) / 8;
	const uint mcuWidth = (header->width + 7) / 8;
	const uint paddingSize = (4 - (header->width * 3) % 4) % 4;
	const uint fileSize = 14 + 12 + header->height * header->width * 3 + header->height * paddingSize;

	size_t byte_count = 0;

	#define PRINT_BYTE_HEX_K(b) do { \
		unsigned _bb = (unsigned)(b) & 0xFFu; \
		/* print two-digit lowercase hex without relying on width support in kprintf */ \
		print_hex2(_bb); \
		kprintf(" "); \
		byte_count++; \
	if ((byte_count % 16) == 0) { kprintln(""); } \
	} while(0)

	/* BITMAPFILEHEADER */
	PRINT_BYTE_HEX_K('B'); PRINT_BYTE_HEX_K('M');
	PRINT_BYTE_HEX_K((fileSize >> 0) & 0xFF); PRINT_BYTE_HEX_K((fileSize >> 8) & 0xFF);
	PRINT_BYTE_HEX_K((fileSize >> 16) & 0xFF); PRINT_BYTE_HEX_K((fileSize >> 24) & 0xFF);
	PRINT_BYTE_HEX_K(0); PRINT_BYTE_HEX_K(0); PRINT_BYTE_HEX_K(0); PRINT_BYTE_HEX_K(0);
	PRINT_BYTE_HEX_K(0x1A); PRINT_BYTE_HEX_K(0); PRINT_BYTE_HEX_K(0); PRINT_BYTE_HEX_K(0);

	/* BITMAPCOREHEADER (12 bytes) */
	PRINT_BYTE_HEX_K(12); PRINT_BYTE_HEX_K(0); PRINT_BYTE_HEX_K(0); PRINT_BYTE_HEX_K(0);
	/* width (2 bytes little endian) */
	PRINT_BYTE_HEX_K((header->width >> 0) & 0xFF); PRINT_BYTE_HEX_K((header->width >> 8) & 0xFF);
	/* height (2 bytes little endian) */
	PRINT_BYTE_HEX_K((header->height >> 0) & 0xFF); PRINT_BYTE_HEX_K((header->height >> 8) & 0xFF);
	/* planes */
	PRINT_BYTE_HEX_K(1); PRINT_BYTE_HEX_K(0);
	/* bitcount 24 */
	PRINT_BYTE_HEX_K(24); PRINT_BYTE_HEX_K(0);

	/* Pixel data bottom-up: B G R */
	for (int i = (int)header->height - 1; i >= 0; --i) {
		const uint mcuRow = i / 8;
		const uint pixelRow = i % 8;
		for (uint j = 0; j < header->width; ++j) {
			const uint mcuColumn = j / 8;
			const uint pixelColumn = j % 8;
			const uint mcuIndex = mcuRow * mcuWidth + mcuColumn;
			const uint pixelIndex = pixelRow * 8 + pixelColumn;
			const MCU *m = &mcus[mcuIndex];
			unsigned char r = (unsigned char)m->y[pixelIndex];
			unsigned char g = (unsigned char)m->cb[pixelIndex];
			unsigned char b = (unsigned char)m->cr[pixelIndex];
			PRINT_BYTE_HEX_K(b);
			PRINT_BYTE_HEX_K(g);
			PRINT_BYTE_HEX_K(r);
		}
		for (uint p = 0; p < paddingSize; ++p) PRINT_BYTE_HEX_K(0);
	}

	if ((byte_count % 16) != 0) kprintln("");
	#undef PRINT_BYTE_HEX_K
}

/* print all info extracted from the JPG file */
void printHeader(Header* header) {
	if (!header) return;
	kprintln("DQT=============");
	for (uint i = 0; i < 4; ++i) {
		if (header->quantizationTables[i].set) {
			kprintln("Table ID: %d", (int)i);
			kprintln("Table Data:");
			for (uint j = 0; j < 64; ++j) {
				/* print value and a trailing space; emit a newline after every 8 values */
				kprintf("%d", (int)header->quantizationTables[i].table[j]);
				kprintf(" ");
				if ((j % 8) == 7) {
					kprintln("");
				}
			}
			/* ensure a blank line after the table */
			kprintln("");
		}
	}
	kprintln("SOF=============");
	/* print frame type as two-digit hex (0xc0 style) */
	kprintf("Frame Type: 0x");
	print_hex2((unsigned)header->frameType);
			kprintln("");
	kprintln("Height: %d", (int)header->height);
	kprintln("Width: %d", (int)header->width);
	kprintln("DHT=============");
	kprintln("DC Tables:");
	for (uint i = 0; i < 4; ++i) {
		if (header->huffmanDCTables[i].set) {
			kprintln("Table ID: %d", (int)i);
			kprintln("Symbols:");
			for (uint j = 0; j < 16; ++j) {
				/* print the symbol index label */
				kprintf("%d: ", (int)(j + 1));
				for (uint k = header->huffmanDCTables[i].offsets[j]; k < header->huffmanDCTables[i].offsets[j + 1]; ++k) {
					unsigned v = (unsigned)header->huffmanDCTables[i].symbols[k];
					/* print as two-digit hex with a space separator */
					print_hex2(v);
					kprintf(" ");
				}
				kprintln("");
			}
		}
	}
	kprintln("AC Tables:");
	for (uint i = 0; i < 4; ++i) {
		if (header->huffmanACTables[i].set) {
			kprintln("Table ID: %d", (int)i);
			kprintln("Symbols:");
			for (uint j = 0; j < 16; ++j) {
				kprintf("%d: ", (int)(j + 1));
				for (uint k = header->huffmanACTables[i].offsets[j]; k < header->huffmanACTables[i].offsets[j + 1]; ++k) {
					unsigned v = (unsigned)header->huffmanACTables[i].symbols[k];
					print_hex2(v);
					kprintf(" ");
				}
				kprintln("");
			}
		}
	}
	kprintln("SOS=============");
	kprintln("Start of Selection: %d", (int)header->startOfSelection);
	kprintln("End of Selection: %d", (int)header->endOfSelection);
	kprintln("Successive Approximation High: %d", (int)header->successiveApproximationHigh);
	kprintln("Successive Approximation Low: %d", (int)header->successiveApproximationLow);
	kprintln("Restart Interval: %d", (int)header->restartInterval);
	kprintln("Color Components:");
	for (uint i = 0; i < header->numComponents; ++i) {
		kprintln("Component ID: %d", (int)(i + 1));
		kprintln("Horizontal Sampling Factor: %d", (int)header->colorComponents[i].horizontalSamplingFactor);
		kprintln("Vertical Sampling Factor: %d", (int)header->colorComponents[i].verticalSamplingFactor);
		kprintln("Quantization Table ID: %d", (int)header->colorComponents[i].quantizationTableID);
		kprintln("Huffman DC Table ID: %d", (int)header->colorComponents[i].huffmanDCTableID);
		kprintln("Huffman AC Table ID: %d", (int)header->colorComponents[i].huffmanACTableID);
	}
	kprintln("Length of Huffman Data: %d", (int)header->huffmanData.size);
}

/////////////////////////////////////////
// End : Debugging / BMP Output Stage
/////////////////////////////////////////


int main(void) {

	uart_init();

	/* Enable FPU by setting FS bits in mstatus */
	asm volatile ("li t0, 0x6000"); // FS = 0b11 (dirty)
	asm volatile ("csrs mstatus, t0"); // add these two lines to enable FPU
	
	/* Clear FCSR to disable FP exception traps */
	asm volatile ("csrwi fcsr, 0");

	/* Initialize memory reader with embedded image bytes. */
	if (embedded_cropped_cat_size == 0) {
		kprintln("Error - no embedded image data available");
		return 1;
	}
	else{
		kprintln("Embedded image data size: %d bytes", (int)embedded_cropped_cat_size);
	}
	/* Provide caller-allocated header storage to allow re-entrant usage and avoid function-static storage. */
	static Header header_storage;
	// long read_cycles1 = read_csr(mcycle);
	Header *header = readJPG(&header_storage, embedded_cropped_cat, embedded_cropped_cat_size);
	// long read_cycles2 = read_csr(mcycle);

	// kprintln("JPG read cycles: %ld", read_cycles2 - read_cycles1);

	if (!header) {
		kprintln("Error - Memory error");
		return 1;
	}
	if (header->valid == false) {
		kprintln("Error - Invalid JPG");
		header_free(header);
		return 1;
	}

	printHeader(header);

	/* Huffman decode using a static MCU buffer suitable for bare-metal targets. */
	// read_cycles1 = read_csr(mcycle);
	MCU* mcus = decodeHuffmanData(header);
	// read_cycles2 = read_csr(mcycle);
	// kprintln("Huffman decode cycles: %ld", read_cycles2 - read_cycles1);
	if (mcus == 0) {
		kprintln("Error - Huffman decode failed");
		header_free(header);
		return 1;
	}

	/* dequantization MCU coefficients (match cpp_version behavior) */
	// read_cycles2 = read_csr(mcycle);
	dequantize(header, mcus);
	// read_cycles2 = read_csr(mcycle);
	// kprintln("Dequantization cycles: %ld", read_cycles2 - read_cycles1);
	/* inverse DCT on all MCUs (match cpp_version / c_version behavior) */
	// read_cycles2 = read_csr(mcycle);
	inverseDCT(header, mcus);
	// read_cycles2 = read_csr(mcycle);
	// kprintln("Vertical Inverse DCT cycles: %ld", read_cycles2 - read_cycles1);

	/* Print BMP bytes (hex) to console. No file I/O is performed to remain bare-metal friendly. */
	printBMP(header, mcus);

	header_free(header);

	kprintln("Program completed successfully.");
	while (1) { /* halt */ }

	return 0;
}
