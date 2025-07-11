// OwnGifReader.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <cstring>
#include <stdint.h>

typedef unsigned char byte;

#define DEBUGGING 1

#define OUTPUTS DEBUGGING


inline int16_t ShortSwap(int16_t l) {
	uint16_t us = *(uint16_t*)&l;

	return
		((us & 0x00FFu) << 8u) |
		((us & 0xFF00u) >> 8u);
}

inline int32_t LongSwap(int32_t l) {
	uint32_t ui = *(uint32_t*)&l;

	return
		((ui & 0x000000FFu) << 24u) |
		((ui & 0x0000FF00u) << 8u) |
		((ui & 0x00FF0000u) >> 8u) |
		((ui & 0xFF000000u) >> 24u);

}

typedef union {
	float f;
	int i;
	unsigned int ui;
} floatint_t;

inline float FloatSwap(const float* f) {
	floatint_t out;

	out.f = *f;
	out.i = LongSwap(out.i);

	return out.f;
}

#if (defined(__BYTE_ORDER) && __BYTE_ORDER == __BIG_ENDIAN)
#pragma message "Assuming Big Endian"
#define LittleShort(x) ShortSwap(x)
#define LittleLong(x) LongSwap(x)
#define LittleFloat(x) FloatSwap(x)
#define BigShort
#define BigLong
#define BigFloat
#else
#pragma message "Assuming Little Endian"
#define BigShort(x) ShortSwap(x)
#define BigLong(x) LongSwap(x)
#define BigFloat(x) FloatSwap(x)
#define LittleShort
#define LittleLong
#define LittleFloat
#endif




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
#define GIFLOCALIMAGEFLAG_SIZEMASK		7
#define GIFLOCALIMAGEFLAG_RESERVED		(3<<3)
#define GIFLOCALIMAGEFLAG_SORT			(1<<5)
#define GIFLOCALIMAGEFLAG_INTERLACED	(1<<6)
#define GIFLOCALIMAGEFLAG_LCT			(1<<7)
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
#define GIF_ERROR(what) *error = (what); CLEANOUTBUFFER; goto cleanup
#define CHECKLENGTH(howmuch) if((howmuch) >= len){GIF_ERROR("read past end of file");}
#define ADVANCE(howmuch) buffer += (size_t)(howmuch); len -= (howmuch)
#define OUT_COPY(howmuch) if(outBufferValid){ memcpy(outBuffer,buffer,(howmuch)); outBuffer+=(howmuch); (*outLen) += (howmuch); } // for copying over when rewriting
#define OUT_PUSHBYTE(b) if(outBufferValid){ *outBuffer=(byte)(b);outBuffer++;(*outLen)++; } // for pushing a single byte when rewriting
#define OUT_PUSHZERO(howmuch) if(outBufferValid){ memset(outBuffer,0,(howmuch)); outBuffer+=(howmuch); (*outLen) += (howmuch); } // for zeroing a bit when rewriting

#define LZW_BUFFER_EXTRABYTES 4

int lzw_getcode(byte* buffer, uint32_t bitoffset, byte bits, int bufferLen) {
	if (bitoffset + bits > ((bufferLen- LZW_BUFFER_EXTRABYTES) << 3)) {
		return -1;
	}

	uint32_t* nudgedBuffer = (uint32_t*)(buffer + ((size_t)bitoffset >> 3));
	uint32_t value = LittleLong(*nudgedBuffer) >> (bitoffset & 7);
	value &= ((1 << bits) - 1);

	return value;
}

static byte roots[256] = { 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255 };

typedef struct gifParsedImage_s {
	byte*	buffer;
	int		bufferSize;
	int		width;
	int		height;
	bool	transparency;
} gifParsedImage_t;

typedef enum gifParseFlags_s {
	GIFPARSE_ALPHA = (1<<0),
	GIFPARSE_FLIPVERT = (1<<1),
	GIFPARSE_BGR = (1<<2)
} gifParseFlags_t;

#define GIF_STRINGIFY(a) #a 
#define GIF_STRINGIFY2(a) GIF_STRINGIFY(a) 
//#define GIF_MAX_RES 1024
#define GIF_DISALLOW_NON_POWER_OF_2 0

// specify outBuffer and outLen if you wish to rewrite the file.
// outbuffer MUST be at least the same length as buffer.
// rgbAOutBuffer will get the address of an rgba buffer
void read_gif(const byte* buffer, size_t len, const char** error, int parseFlags, byte* outBuffer = NULL, size_t* outLen = NULL, gifParsedImage_t* outImage = NULL) {
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

	if (outImage) {
		memset(outImage, 0, sizeof(gifParsedImage_t));
	}

	if (false) { // yea this is ugly lol. i put it at the top so that the goto doesn't jump down bypassing initializations
	cleanup:
		if (codes) {
			delete[] codes;
		}
		if (imageIndices) {
			delete[] imageIndices;
		}
		if (imageData && (!outImage || !outImage->buffer)) { // only clean up if we didn't end up actually returning it
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
		GIF_ERROR("GIF decode argument error: Can only specify both outBuffer and outLen or neither. Specifying only one is invalid.");
	}
	if (!outBuffer && !outImage) {
		GIF_ERROR("GIF decode argument error: Neither outBuffer nor rgbAOutBuffer are specified. Aborting.");
	}

	CLEANOUTBUFFER;

	if (sizeof(gifHeader_t) >= len) {
		GIF_ERROR("File shorter than header");
	}
	gifHeader_t* header = (gifHeader_t*)buffer;

	OUT_COPY(10); // everything in header before flags
	// correct outFlags to prevent any shenanigans
	byte outFlags = header->flags;
	outFlags &= ~(GIFHEADERFLAG_SORT); // this won't crash US but who says it might not crash some 1960s GIF decoder if we allow it
	if (outBufferValid && ((header->flags & GIFHEADERFLAG_RESMASK) >> 4) < (header->flags & GIFHEADERFLAG_SIZEMASK)) {
		outFlags &= ~(GIFHEADERFLAG_RESMASK); // this won't crash US but who says it won't crash some other random GIF reader if we let it be smaller than gct Size
		outFlags |= ((header->flags & GIFHEADERFLAG_SIZEMASK) << 4);
	}
	ADVANCE(11);
	OUT_PUSHBYTE(outFlags);
	OUT_COPY(2);  // everything in header after flags
	ADVANCE(2);


	header->width = LittleShort(header->width);
	header->height = LittleShort(header->height);

	if (memcmp(header->magic, "GIF", sizeof(header->magic)) || memcmp(header->version, "87a", sizeof(header->version)) && memcmp(header->version, "89a", sizeof(header->version))) {
		GIF_ERROR("GIF magic/version incorrect");
	}
	if (header->width <= 0 || header->height <= 0) {
		GIF_ERROR("GIF resolution must not be 0");
	}
	if (header->width * header->height < 2) {  // idk what c++ will do across platforms if i allocate a 1-sized array and delete with []
		GIF_ERROR("GIF pixel count must be bigger than 1");
	}
#if GIF_MAX_RES
	if (header->width > GIF_MAX_RES || header->height> GIF_MAX_RES) {
		GIF_ERROR("GIF too big, max resolution is " GIF_STRINGIFY2(GIF_MAX_RES) "x" GIF_STRINGIFY2(GIF_MAX_RES) "");
	}
#endif
#if GIF_DISALLOW_NON_POWER_OF_2
	if ((header->width & (header->width - 1)) || (header->height & (header->height - 1))) {
		GIF_ERROR("GIF not a power of 2");
	}
#endif

#if OUTPUTS>=2
	std::cout << "Gif resolution: " << header->width << "x" << header->height << "\n";
#endif

	if (header->flags & GIFHEADERFLAG_GCT) {
		gctLen = 1 << ((header->flags & GIFHEADERFLAG_SIZEMASK) + 1);
		int	res = 1 << (((header->flags & GIFHEADERFLAG_RESMASK) >> 4) + 1);
		if (header->bgColor >= gctLen) {
			GIF_ERROR("Transparent color index higher or equal to global color table length.");
		}
		if (gctLen * sizeof(colorvec_t) >= len) {
			GIF_ERROR("GIF not long enough to hold global color table");
		}
		gct = new colorvec_t[gctLen];
		if (!gct) {
			GIF_ERROR("GIF decode error: Unable to allocate global color table");
		}
		memcpy(gct, buffer, gctLen * sizeof(colorvec_t));
		OUT_COPY(gctLen * sizeof(colorvec_t));
		ADVANCE(gctLen * sizeof(colorvec_t));
	}
	else {
		GIF_ERROR("GIF has no global color table");
	}


	// skip extensions
	CHECKLENGTH(1); byte type = *(byte*)buffer;	ADVANCE(1);
	byte helper;
	while (type == 0x21) {
		CHECKLENGTH(1); byte extensionType = *(byte*)buffer;	ADVANCE(1);
		switch (extensionType) {
		default:
			GIF_ERROR("GIF extension type invalid");

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
					GIF_ERROR("Transparent color index higher or equal to global color table length.");
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
		GIF_ERROR("GIF corrupted/no image found");
	}

	OUT_PUSHBYTE(type);

	// the actual image
	CHECKLENGTH(sizeof(gifLocalImage_t));
	gifLocalImage_t* localImage = (gifLocalImage_t*)buffer;
	OUT_COPY(8); // everything in local image struct before flags
	outFlags = localImage->flags;
	outFlags &= ~GIFLOCALIMAGEFLAG_SORT;  // this won't crash US but who says it might not crash some 1960s GIF decoder if we allow it
	outFlags &= ~GIFLOCALIMAGEFLAG_RESERVED;  // same as above.
	OUT_PUSHBYTE(outFlags);
	ADVANCE(sizeof(gifLocalImage_t) - 1); // -1 cuz struct alignment. SIGH

	localImage->top = LittleShort(localImage->top);
	localImage->left = LittleShort(localImage->left);
	localImage->width = LittleShort(localImage->width);
	localImage->height = LittleShort(localImage->height);

	if (localImage->width <= 0 || localImage->height <= 0) {
		GIF_ERROR("GIF local image resolution must not be 0");
	}
	if (localImage->width * localImage->height < 2) { // idk what c++ will do across platforms if i allocate a 1-sized array and delete with []
		GIF_ERROR("GIF local image pixel count must be bigger than 1");
	}
#if GIF_MAX_RES
	if (localImage->width > GIF_MAX_RES || localImage->height > GIF_MAX_RES) {
		GIF_ERROR("GIF local image too big, max resolution is " GIF_STRINGIFY2(GIF_MAX_RES) "x" GIF_STRINGIFY2(GIF_MAX_RES) "");
	}
#endif

	if (localImage->flags & GIFLOCALIMAGEFLAG_LCT) {
		GIF_ERROR("GIF with local color table not supported");
		// untested, don't do it for now
		/*lctLen = 1 << ((localImage->flags & GIFLOCALIMAGEFLAG_SIZEMASK) + 1);
		if (lctLen * sizeof(colorvec_t) >= len) {
			GIF_ERROR("GIF not long enough to hold local color table");
		}
		lct = new colorvec_t[lctLen];
		if (!lct) {
			GIF_ERROR("GIF decode error: Unable to allocate local color table");
		}
		memcpy(lct, buffer, lctLen * sizeof(colorvec_t));
		ADVANCE(lctLen * sizeof(colorvec_t) - 1); // -1 because the meaningful data is actually 13 bytes but struct auto aligns due to the uint16_t
		*/
	}
	if (localImage->flags & GIFLOCALIMAGEFLAG_INTERLACED) {
		//GIF_ERROR("GIF with interlacing not supported"); // might add later
	}

	if (localImage->width + localImage->left > header->width || localImage->height + localImage->top > header->height) {
		GIF_ERROR("GIF local image breaks bounds of GIF");
	}

	CHECKLENGTH(1); byte lzwMinCodeSize = *(byte*)buffer; OUT_PUSHBYTE(lzwMinCodeSize); ADVANCE(1);
	if (lzwMinCodeSize >= 12) {
		GIF_ERROR("GIF LZW min code size wants to be >= 12 bits. Nonsense.");
	}
	int lzwMinCodeCount = 1 << lzwMinCodeSize;

	if (lzwMinCodeCount < gctLen) {
		GIF_ERROR("GIF LZW min code size wants to be smaller than required for the global color table.");
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
	codes = new byte[codecount + LZW_BUFFER_EXTRABYTES]; // +4 so we can comfortably read variable-width bits through uint32_t casting without worrying about an overflow.
	if (!codes) {
		GIF_ERROR("GIF decode error: Unable to allocate code array");
	}

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
	if (!imageIndices) {
		GIF_ERROR("GIF decode error: Unable to allocate global image indices");
	}

	size_t bitoffset = 0;
	int codewidth = lzwMinCodeSize + 1;
	int code, lastCode = -1;
	bool done = false;
	size_t outIndex = 0;
	for (i = 0, bitoffset = 0; !done; i++) {
		if ((nextCodeEntry - codeTable) & ~((1 << codewidth) - 1) && codewidth < 12) {
			codewidth++;
		}
		code = lzw_getcode(codes, bitoffset, codewidth, codecount + LZW_BUFFER_EXTRABYTES);
#if OUTPUTS>=2
		std::cout << "LZW code: " << code << "\n";
#endif
		bitoffset += codewidth;
		if (code < 0) {
			GIF_ERROR("GIF Error reading LZW code.");
		}
		else if (code > 4095) {
			GIF_ERROR("GIF Error reading LZW code (>4095).");
		}
		switch (codeTable[code].type) {
		case LZWCODE_USED:
			if ((outIndex + codeTable[code].len - 1) >= localImagePixels) {
				GIF_ERROR("GIF Error decoding LZW, overflew local image buffer at LZWCODE_USED.");
			}
			memcpy(imageIndices + outIndex, codeTable[code].data, codeTable[code].len);
			outIndex += codeTable[code].len - 1;
			if (lastCode >= 0 && nextCodeEntry != codeTableEnd) {
				int newLen = codeTable[lastCode].len + 1;
				byte* newSymbol = imageIndices + outIndex - (codeTable[lastCode].len + codeTable[code].len) + 1;
				if (newSymbol < imageIndices) {
					// can this even happen? idk
					GIF_ERROR("GIF Error decoding LZW, underflew local image buffer at LZWCODE_USED. Wtf that's sophisticated.");
				}
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
					GIF_ERROR("GIF Error decoding LZW, next code entry not equal to code.");
				}
				int newLen = codeTable[lastCode].len + 1;
				if ((outIndex + newLen - 1) >= localImagePixels) {
					GIF_ERROR("GIF Error decoding LZW, overflew local image buffer at LZWCODE_EMPTY.");
				}
				if (nextCodeEntry == codeTableEnd) {
					GIF_ERROR("GIF Error decoding LZW, table full but code not in dictionary. WTF.");
				}
				memcpy(imageIndices + outIndex, codeTable[lastCode].data, codeTable[lastCode].len);
				outIndex += codeTable[lastCode].len;
				imageIndices[outIndex] = codeTable[lastCode].data[0];
				byte* newSymbol = imageIndices + outIndex - newLen + 1;
				if (newSymbol < imageIndices) {
					// can this even happen? idk
					GIF_ERROR("GIF Error decoding LZW, underflew local image buffer at LZWCODE_EMPTY. Wtf that's sophisticated.");
				}
				nextCodeEntry->type = LZWCODE_USED;
				nextCodeEntry->data = newSymbol;
				nextCodeEntry->len = newLen;
				nextCodeEntry++;
				outIndex++;
			}
			else {
				GIF_ERROR("GIF LZW decoding error: Referenced empty code table entry without lastCode");
			}
			break;
		case LZWCODE_GAP:
			GIF_ERROR("GIF LZW decoding error: Referenced gap in code table");
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

	if (bitoffset + 8 <= (codecount << 3)) {
		GIF_ERROR("GIF decode error: More LZW codes than needed");
	}
	if (outIndex != localImagePixels) {
		GIF_ERROR("GIF decode error: Decoded pixel count smaller than output image buffer");
	}

	if (!outImage) {
		// we were only asked to rewrite the image. No need to fully decode it.
		return;
	}

	if (localImage->flags & GIFLOCALIMAGEFLAG_INTERLACED) {
		byte* imageIndicesDeinterlaced = new byte[localImagePixels];
		if (!imageIndicesDeinterlaced) {
			GIF_ERROR("GIF decode error: Unable to allocate deinterlaced index array");
		}
		int inRow = 0;
		for (int y = 0; y < localImage->height && inRow < localImage->height; y+=8) {
			memcpy(imageIndicesDeinterlaced + localImage->width*y, imageIndices + localImage->width * inRow,localImage->width); inRow++;
		}
		for (int y = 4; y < localImage->height && inRow < localImage->height; y+=8) {
			memcpy(imageIndicesDeinterlaced + localImage->width*y, imageIndices + localImage->width * inRow,localImage->width); inRow++;
		}
		for (int y = 2; y < localImage->height && inRow < localImage->height; y+=4) {
			memcpy(imageIndicesDeinterlaced + localImage->width*y, imageIndices + localImage->width * inRow,localImage->width); inRow++;
		}
		for (int y = 1; y < localImage->height && inRow < localImage->height; y+=2) {
			memcpy(imageIndicesDeinterlaced + localImage->width*y, imageIndices + localImage->width * inRow,localImage->width); inRow++;
		}
		delete[] imageIndices;
		imageIndices = imageIndicesDeinterlaced;
	}

	int pixelWidth = (parseFlags & GIFPARSE_ALPHA) ? sizeof(colorvec_t) + 1 : sizeof(colorvec_t);
	int stride = pixelWidth * (size_t)header->width;
	int outBufferSize =  (size_t)header->height * stride;
	imageData = new byte[outBufferSize];
	if (!imageData) {
		GIF_ERROR("GIF decode error: Unable to allocate RGB(A) image data");
	}

	if (!gct) {
		GIF_ERROR("GIF decoding failed. No GCT for some reason.");
	}

	// is this safe? it should be... could do some bounds checks but unless i really messed up nothing should really go wrong here.
	int colorIndex;
	int rOffset = (parseFlags & GIFPARSE_BGR) ? 2 : 0;
	int bOffset = (parseFlags & GIFPARSE_BGR) ? 0 : 2;
	int bufferY = (parseFlags & GIFPARSE_FLIPVERT) ? header->height -1 : 0;
	int bufferYIncrement = (parseFlags & GIFPARSE_FLIPVERT) ? -1 : 1;
	for (int y = 0; y < header->height; y++,bufferY += bufferYIncrement) {
		for (int x = 0; x < header->width; x++) {
			if (x < localImage->left || x >= (localImage->left+localImage->width) || y < localImage->top || y >= (localImage->top + localImage->height)) {
				colorIndex = header->bgColor;
			}
			else {
				colorIndex = imageIndices[(y-localImage->top)*localImage->width + (x-localImage->left)];
			}

			imageData[bufferY * stride + x * pixelWidth + rOffset] = gct[colorIndex][0];
			imageData[bufferY * stride + x * pixelWidth + 1] = gct[colorIndex][1];
			imageData[bufferY * stride + x * pixelWidth + bOffset] = gct[colorIndex][2];
			if (parseFlags & GIFPARSE_ALPHA) {
				imageData[bufferY * stride + x * pixelWidth + 3] = colorIndex == transparentColorIndex ? 0 : 255;
			}
		}
	}

	outImage->buffer = imageData;
	outImage->bufferSize = outBufferSize;
	outImage->width = header->width;
	outImage->height = header->height;
	outImage->transparency = transparentColorIndex != -1;


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
		if (!buffer) return 1;
		size_t read = 0;
		while (read < len) {
			read += fread(buffer+read, 1, len-read, f);
		};
		fclose(f);

		byte* outbuffer = new byte[len];
		if (!outbuffer) {
			delete[] buffer;
			return 1;
		}
		size_t outLen = 0;

		gifParsedImage_t gifImage = { 0 };

		int parseFlags = GIFPARSE_ALPHA;
		if (parseFlags & GIFPARSE_ALPHA) {
			parseFlags |= GIFPARSE_FLIPVERT | GIFPARSE_BGR;
		}

		read_gif(buffer, len, &error, parseFlags, outbuffer, &outLen,&gifImage);

		if(gifImage.buffer){
#if DEBUGGING
			if (!error) {
				FILE* g = NULL;
				if (!(parseFlags & GIFPARSE_ALPHA)) {
					if (!fopen_s(&g, "testdecode.ppm", "wb") && g) {
						char buf[40];
						buf[0] = '\0';
						sprintf_s(buf, sizeof(buf), "P6\n%d %d\n255\n", gifImage.width, gifImage.height);
						fwrite(buf, 1, strlen(buf), g);
						size_t written = 0;
						while (written < gifImage.bufferSize) {
							written += fwrite(gifImage.buffer + written, 1, gifImage.bufferSize - written, g);
						};
						fclose(g);
					}
				}
				else {
					if (!fopen_s(&g, "testdecode.tga", "wb") && g) {
						byte buf[18];
						memset(buf, 0, sizeof(buf));
						buf[2] = 2;
						buf[7] = 32;
						buf[12] = gifImage.width & 0x00FF;
						buf[13] = (gifImage.width & 0xFF00) >> 8;
						buf[14] = gifImage.height & 0x00FF;
						buf[15] = (gifImage.height & 0xFF00) >> 8;
						buf[16] = 32;

						size_t written = 0;
						while (written < sizeof(buf)) {
							written += fwrite(buf + written, 1, sizeof(buf) - written, g);
						};
						written = 0;
						while (written < gifImage.bufferSize) {
							written += fwrite(gifImage.buffer + written, 1, gifImage.bufferSize - written, g);
						};
						fclose(g);
					}
				}
			}
#endif

			delete[] gifImage.buffer;
		}

#if DEBUGGING
		if (!error && outLen > 0) {
			FILE* g = NULL;
			if (!fopen_s(&g, "teststripped.gif", "wb") && g) {
				size_t written = 0;
				while (written < outLen) {
					written += fwrite(outbuffer+written, 1, outLen-written, g);
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
