// OwnGifReader.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <cstring>
#include <stdint.h>

typedef unsigned char byte;

#define OUTPUTS 2

typedef struct gifHeader_s {
	byte		magic[3];
	byte		version[3];
	// logical screen descriptor
	uint16_t	width;
	uint16_t	height;
#define GIFHEADERFLAG_SIZEMASK	7
#define GIFHEADERFLAG_SORT		(1<<3)
#define GIFHEADERFLAG_RESMASK	(7<<4)
#define GIFHEADERFLAG_GCT		(1<<7)
	byte		flags;
	byte		bgColor;
	byte		aspect;
	byte		filler; // struct has to align to the uint16_t, yikes. so just subtract 1 when advancing.
} gifHeader_t;

typedef struct gifLocalImage_s {
	uint16_t	left;
	uint16_t	top;
	uint16_t	width;
	uint16_t	height;
#define GIFLOCALIMAGEFLAG_LCT			1
#define GIFLOCALIMAGEFLAG_INTERLACED	2
#define GIFLOCALIMAGEFLAG_SORT			4
#define GIFLOCALIMAGEFLAG_RESERVED		(3<<3)
#define GIFLOCALIMAGEFLAG_SIZEMASK		(7<<4)
	byte		flags;
	byte		filler; // struct has to align to the uint16_t, yikes. so just subtract 1 when advancing.
} gifLocalImage_t;

typedef enum codeEntryType_s {
	LZWCODE_EMPTY,
	//LZWCODE_ROOT,
	LZWCODE_GAP,
	LZWCODE_USED,
	LZWCODE_CLEAR,
	LZWCODE_EOF,
} codeEntryType_t;

typedef struct codeEntry_s {
	byte* data;
	uint32_t			len; // is this valid?
	codeEntryType_t		type;
} codeEntry_t;


typedef	byte color_t;
typedef	color_t colorvec_t[3];

#define CLEANOUTBUFFER if(outBufferValid) { memset(outBufStart,0,len); *outLen = 0; outBuffer=outBufStart; }
#define ERROR(what) *error = (what); CLEANOUTBUFFER; goto cleanup
#define CHECKLENGTH(howmuch) if((howmuch) >= len){ERROR("read past end of file");}
#define ADVANCE(howmuch) buffer += (size_t)(howmuch); len -= (howmuch)
#define OUT_COPY(howmuch) if(outBufferValid){ memcpy(outBuffer,buffer,(howmuch)); outBuffer+=(howmuch); (*outLen) += (howmuch); } // for copying over when rewriting
#define OUT_PUSHBYTE(b) if(outBufferValid){ *outBuffer=(byte)(b);outBuffer++;(*outLen)++; } // for pushing a single byte when rewriting
#define OUT_PUSHZERO(howmuch) if(outBufferValid){ memset(outBuffer,0,(howmuch)); outBuffer+=(howmuch); (*outLen) += (howmuch); } // for zeroing a bit when rewriting

int lzw_getcode(byte* buffer, uint32_t bitoffset, byte bits, int bufferLen) {
	if (bitoffset + bits > (bufferLen << 3)) {
		return -1;
	}

	uint32_t* nudgedBuffer = (uint32_t*)(buffer + ((size_t)bitoffset >> 3));
	uint32_t value = *nudgedBuffer >> (bitoffset & 7);
	value &= ((1 << bits) - 1);

	return value;
}

static byte roots[256] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255 };

// specify outBuffer and outLen if you wish to rewrite the file.
// outbuffer MUST be at least the same length as buffer.
void read_gif(const byte* buffer, size_t len, const char** error, byte* outBuffer = NULL, size_t* outLen = NULL) {
	colorvec_t* gct = NULL;
	int			gctLen = 0;
	colorvec_t* lct = NULL;
	int			lctLen = 0;
	int			transparentColorIndex = -1;
	byte* outBufStart = outBuffer;

	// for later
	byte* imageIndices = NULL;
	byte* imageData = NULL;
	byte* codes = NULL;

	if (false) { // yea this is ugly lol. i put it at the top so that the goto doesn't jump down bypassing initializations
	cleanup:
		if (codes) {
			delete[] codes;
		}
		if (imageIndices) {
			delete[] imageIndices;
		}
		if (imageData) {
			delete[] imageData;
		}
		if (lct) {
			delete[] lct;
		}
		if (gct) {
			delete[] gct;
		}
		return;
	}

	bool outBufferValid = outBuffer && outLen;

	if (!outBuffer != !outLen) {
		ERROR("GIF decode argument error: Can only specify both outBuffer and outLen or neither. Specifying only one is invalid.");
	}

	CLEANOUTBUFFER;

	if (sizeof(gifHeader_t) >= len) {
		ERROR("File shorter than header");
	}
	gifHeader_t* header = (gifHeader_t*)buffer;
	OUT_COPY(sizeof(gifHeader_t) - 1);
	ADVANCE(sizeof(gifHeader_t) - 1); // -1 because the meaningful data is actually 13 bytes but struct auto aligns due to the uint16_t


	if (memcmp(header->magic, "GIF", sizeof(header->magic)) || memcmp(header->version, "87a", sizeof(header->version)) && memcmp(header->version, "89a", sizeof(header->version))) {
		ERROR("GIF magic/version incorrect");
	}
	if ((header->width & (header->width - 1)) || (header->height & (header->height - 1))) {
		//ERROR("GIF not a power of 2");
	}

#if OUTPUTS>=2
	std::cout << "Gif resolution: " << header->width << "x" << header->height << "\n";
#endif

	if (header->flags & GIFHEADERFLAG_GCT) {
		gctLen = 1 << ((header->flags & GIFHEADERFLAG_SIZEMASK) + 1);
		int	res = 1 << (((header->flags & GIFHEADERFLAG_RESMASK) >> 4) + 1);
		if (header->bgColor >= gctLen) {
			ERROR("Transparent color index higher or equal to global color table length.");
		}
		if (gctLen * sizeof(colorvec_t) >= len) {
			ERROR("GIF not long enough to hold global color table");
		}
		gct = new colorvec_t[gctLen];
		memcpy(gct, buffer, gctLen * sizeof(colorvec_t));
		OUT_COPY(gctLen * sizeof(colorvec_t));
		ADVANCE(gctLen * sizeof(colorvec_t));
	}


	// skip extensions
	CHECKLENGTH(1); byte type = *(byte*)buffer;	ADVANCE(1);
	byte helper;
	while (type == 0x21) {
		CHECKLENGTH(1); byte extensionType = *(byte*)buffer;	ADVANCE(1);
		switch (extensionType) {
		default:
			ERROR("GIF extension type invalid");

		case 0x01: // Plain text extension
			CHECKLENGTH(13); ADVANCE(13);
			helper = 1;
			while (helper) {
				helper = *(byte*)buffer; // bytes in block
				CHECKLENGTH((size_t)helper + 1); ADVANCE((size_t)helper + 1);
			}
			break;
		case 0xF9: // Graphical control extension. We only care about whether we have a transparent color, rest we ignore. We don't support animations etc.

			// this is the only extension we preserve due to it indicating transparency. we default all of the other data tho, just to be safe.
			OUT_PUSHBYTE(type);
			OUT_PUSHBYTE(extensionType);

			CHECKLENGTH(6);
			ADVANCE(1); OUT_PUSHBYTE(4); // skip blocksize (push out default value)
			helper = *(byte*)buffer; // flags
			ADVANCE(3); // advance flags and skip delayTime
			if (helper & 1) {
				transparentColorIndex = *(byte*)buffer;
				if (transparentColorIndex >= gctLen) {
					ERROR("Transparent color index higher or equal to global color table length.");
				}
				OUT_PUSHBYTE(1); OUT_PUSHZERO(2); OUT_COPY(1); OUT_PUSHBYTE(0);
			}
			else {
				OUT_PUSHBYTE(0); OUT_PUSHZERO(4);
			}
			ADVANCE(2); // advance colorIndex and skip terminator
			break;
		case 0xFE: // Comment extension 
			helper = 1;
			while (helper) {
				helper = *(byte*)buffer; // bytes in block
				CHECKLENGTH((size_t)helper + 1); ADVANCE((size_t)helper + 1);
			}
			break;
		case 0xFF: // Application extension (XMP data or random shit)
			CHECKLENGTH(12); ADVANCE(12);
			helper = 1;
			while (helper) {
				helper = *(byte*)buffer; // bytes in block
				CHECKLENGTH((size_t)helper + 1); ADVANCE((size_t)helper + 1);
			}
			break;

		}

		CHECKLENGTH(1); type = *(byte*)buffer;	ADVANCE(1);
	}

	if (type != 0x2C) {
		ERROR("GIF corrupted/no image found");
	}

	OUT_PUSHBYTE(type);

	// the actual image
	CHECKLENGTH(sizeof(gifLocalImage_t));
	gifLocalImage_t* localImage = (gifLocalImage_t*)buffer;
	OUT_COPY(sizeof(gifLocalImage_t) - 1);
	ADVANCE(sizeof(gifLocalImage_t) - 1); // -1 cuz struct alignment. SIGH

	if (localImage->flags & GIFLOCALIMAGEFLAG_LCT) {
		ERROR("GIF with local color table not supported");
		// untested, don't do it for now
		/*lctLen = 1 << ((localImage->flags & GIFLOCALIMAGEFLAG_SIZEMASK) + 1);
		if (lctLen * sizeof(colorvec_t) >= len) {
			ERROR("GIF not long enough to hold local color table");
		}
		lct = new colorvec_t[lctLen];
		memcpy(lct, buffer, lctLen * sizeof(colorvec_t));
		ADVANCE(lctLen * sizeof(colorvec_t) - 1); // -1 because the meaningful data is actually 13 bytes but struct auto aligns due to the uint16_t
		*/
	}
	if (localImage->flags & GIFLOCALIMAGEFLAG_INTERLACED) {
		ERROR("GIF with interlacing not supported"); // might add later
	}

	if (localImage->width + localImage->left > header->width || localImage->height + localImage->top > header->height) {
		ERROR("GIF local image breaks bounds of GIF");
	}

	CHECKLENGTH(1); byte lzwMinCodeSize = *(byte*)buffer; OUT_PUSHBYTE(lzwMinCodeSize); ADVANCE(1);
	if (lzwMinCodeSize >= 12) {
		ERROR("GIF LZW min code size wants to be >= 12 bits. Nonsense.");
	}
	int lzwMinCodeCount = 1 << lzwMinCodeSize;

	if (lzwMinCodeCount < gctLen) {
		ERROR("GIF LZW min code size wants to be smaller than required for the global colorr table.");
	}


	size_t	oldLen = len;
	const byte* oldBuffer = buffer;
	// we quickly fast-forward through the buffer to see how many codes we have in total
	int codecount = 0;
	helper = 1;
	while (helper) {
		helper = *(byte*)buffer; // bytes in block
		codecount += helper;
		CHECKLENGTH((size_t)helper + 1); ADVANCE((size_t)helper + 1);
	}

	// rewind, make the buffer and go again
	len = oldLen;
	buffer = oldBuffer;
	codes = new byte[codecount + 4]; // +4 so we can comfortably read variable-width bits through uint32_t casting without worrying about an overflow.

	helper = 1;
	codecount = 0;
	while (helper) {
		helper = *(byte*)buffer; // bytes in block
		CHECKLENGTH((size_t)helper + 1);
		memcpy(codes + codecount, buffer + 1, helper);
		codecount += helper;
		OUT_COPY((size_t)helper + 1);
		ADVANCE((size_t)helper + 1);
	}

	OUT_PUSHBYTE(0x3B); // and that's it. out file is finished.


	codeEntry_t codeTable[4096] = { 0 }; // 12 bit code size so maximum 4096 entries.

	// Init dictionary
	int i;
	for (i = 0; i < gctLen; i++) {
		codeTable[i].type = LZWCODE_USED;
		codeTable[i].len = 1;
		codeTable[i].data = &roots[i];
	}
	for (; i < lzwMinCodeCount; i++) {
		codeTable[i].type = LZWCODE_GAP;
		codeTable[i].len = 1;
	}
	codeTable[i++].type = LZWCODE_CLEAR;
	codeTable[i++].type = LZWCODE_EOF;

	codeEntry_t* codeTableEnd = &codeTable[4095] + 1;
	codeEntry_t* extraCodeEntries = &codeTable[i];
	codeEntry_t* nextCodeEntry = extraCodeEntries;

	size_t localImagePixels = (size_t)localImage->width * (size_t)localImage->height;
	imageIndices = new byte[localImagePixels];

	size_t bitoffset = 0;
	int codewidth = lzwMinCodeSize + 1;
	int code, lastCode = -1;
	bool done = false;
	size_t outIndex = 0;
	for (i = 0, bitoffset = 0; !done; i++) {
		if ((nextCodeEntry - codeTable) & ~((1 << codewidth) - 1) && codewidth < 12) {
			codewidth++;
		}
		code = lzw_getcode(codes, bitoffset, codewidth, codecount + 4);
#if OUTPUTS>=2
		std::cout << "LZW code: " << code << "\n";
#endif
		bitoffset += codewidth;
		if (code < 0) {
			ERROR("GIF Error reading LZW code.");
		}
		else if (code > 4095) {
			ERROR("GIF Error reading LZW code (>4095).");
		}
		switch (codeTable[code].type) {
		case LZWCODE_USED:
			if ((outIndex + codeTable[code].len - 1) >= localImagePixels) {
				ERROR("GIF Error decoding LZW, overflew local image buffer at used.");
			}
			memcpy(imageIndices + outIndex, codeTable[code].data, codeTable[code].len);
			outIndex += codeTable[code].len - 1;
			if (lastCode >= 0 && nextCodeEntry != codeTableEnd) {
				int newLen = codeTable[lastCode].len + 1;
				byte* newSymbol = imageIndices + outIndex - (codeTable[lastCode].len + codeTable[code].len) + 1;
				nextCodeEntry->type = LZWCODE_USED;
				nextCodeEntry->data = newSymbol;
				nextCodeEntry->len = newLen;
				nextCodeEntry++;
			}
			outIndex++;
			break;
		case LZWCODE_EMPTY:
			if (lastCode >= 0) {
				if (nextCodeEntry - codeTable != code) {
					ERROR("GIF Error decoding LZW, next code entry not equal to code.");
				}
				int newLen = codeTable[lastCode].len + 1;
				if ((outIndex + newLen - 1) >= localImagePixels) {
					ERROR("GIF Error decoding LZW, overflew local image buffer at empty.");
				}
				if (nextCodeEntry == codeTableEnd) {
					ERROR("GIF Error decoding LZW, table full but code not in dictionary. WTF.");
				}
				memcpy(imageIndices + outIndex, codeTable[lastCode].data, codeTable[lastCode].len);
				outIndex += codeTable[lastCode].len;
				imageIndices[outIndex] = codeTable[lastCode].data[0];
				byte* newSymbol = imageIndices + outIndex - newLen + 1;
				nextCodeEntry->type = LZWCODE_USED;
				nextCodeEntry->data = newSymbol;
				nextCodeEntry->len = newLen;
				nextCodeEntry++;
				outIndex++;
			}
			else {
				ERROR("GIF LZW decoding error: Referenced empty code table entry without lastCode");
			}
			break;
		case LZWCODE_GAP:
			ERROR("GIF LZW decoding error: Referenced gap in code table");
			break;
		case LZWCODE_EOF:
			done = true;
			break;
		case LZWCODE_CLEAR:
			memset(extraCodeEntries, 0, (byte*)codeTableEnd - (byte*)extraCodeEntries);
			nextCodeEntry = extraCodeEntries;
			codewidth = lzwMinCodeSize + 1;
			lastCode = -1;
			goto nolastcode;
			break;
		}
		lastCode = code;
	nolastcode:
		continue;
	}

	imageData = new byte[(size_t)header->width * (size_t)header->height * sizeof(colorvec_t)];


	goto cleanup;

}

#if _WIN32
#define INSTRUMENTATION_FUNC_PROPS  __declspec(noinline) __declspec(dllexport)
#else
#define INSTRUMENTATION_FUNC_PROPS  __attribute__ ((noinline)) __attribute__((dllexport))
#define fopen_s(pFile,filename,mode) ((*(pFile))=fopen((filename),  (mode)))==NULL
#endif

extern "C"  INSTRUMENTATION_FUNC_PROPS int loadfile(const char* file) {
	FILE* f = NULL;
	const char* error = NULL;
#if OUTPUTS
	std::cout << file << "\n";
#endif
	if (!fopen_s(&f, file, "rb") && f) {
		fseek(f, 0, SEEK_END);
		size_t len = ftell(f);
		fseek(f, 0, SEEK_SET);
		byte* buffer = new byte[len];
		size_t read = 0;
		while (read < len) {
			read += fread(buffer, 1, len, f);
		};
		fclose(f);

		byte* outbuffer = new byte[len];
		size_t outLen = 0;

		read_gif(buffer, len, &error, outbuffer, &outLen);

#if 0
		if (!error && outLen > 0) {
			FILE* g = NULL;
			if (!fopen_s(&g, "teststripped.gif", "wb") && g) {
				size_t written = 0;
				while (written < outLen) {
					written += fwrite(outbuffer, 1, outLen, g);
				};
				fclose(g);
			}
		}

#endif

#if OUTPUTS
		if (error) {
			std::cout << error << "\n";
		}
#endif
		delete[] buffer;
		delete[] outbuffer;
	}
	return 0;
}

int main(int argc, char* argv[])
{
	loadfile(argv[1]);
}
