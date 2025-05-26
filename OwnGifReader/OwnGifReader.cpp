// OwnGifReader.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>

typedef unsigned char byte;

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


typedef	byte color_t;
typedef	color_t colorvec_t[3];

#define CHECKLENGTH(howmuch) if((howmuch) >= len){return;}
#define ADVANCE(howmuch) buffer += (size_t)(howmuch); len -= (howmuch)

void read_gif(const byte* buffer, size_t len) {
	colorvec_t* gct = NULL;
	int			gctLen = 0;
	colorvec_t* lct = NULL;
	int			lctLen = 0;
	int			transparentColorIndex = -1;
	if (sizeof(gifHeader_t) >= len) {
		return;
	}
	gifHeader_t*	header = (gifHeader_t*)buffer;
	ADVANCE(sizeof(gifHeader_t));

	if (memcmp(header->magic,"GIF",sizeof(header->magic)) || memcmp(header->version, "87a", sizeof(header->version)) && memcmp(header->version, "89a", sizeof(header->version))) {
		return;
	}
	if ((header->width & (header->width - 1)) || (header->height & (header->height - 1))) {
		//return; // not power of 2. not interesting for us
	}

	if (header->flags & GIFHEADERFLAG_GCT) {
		gctLen = 1 << ((header->flags & GIFHEADERFLAG_SIZEMASK)+1);
		int	res = 1 << (((header->flags & GIFHEADERFLAG_RESMASK) >> 4) + 1);
		if (gctLen * sizeof(colorvec_t) >= len) {
			return;
		}
		gct = new colorvec_t[gctLen];
		memcpy(gct,buffer, gctLen * sizeof(colorvec_t));
		ADVANCE(gctLen * sizeof(colorvec_t) -1 ); // -1 because the meaningful data is actually 13 bytes but struct auto aligns due to the uint16_t
	}


	// skip extensions
	CHECKLENGTH(1); byte type = *(byte*)buffer;	ADVANCE(1);
	byte helper;
	while (type == 0x21) {
		CHECKLENGTH(1); byte extensionType = *(byte*)buffer;	ADVANCE(1);
		switch (extensionType) {
		default:
			return;

		case 0x01: // Plain text extension
			CHECKLENGTH(13); ADVANCE(13);
			helper = 1;
			while (helper) {
				helper = *(byte*)buffer; // bytes in block
				CHECKLENGTH((size_t)helper + 1); ADVANCE((size_t)helper + 1);
			}
			break;
		case 0xF9: // Graphical control extension. We only care about whether we have a transparent color, rest we ignore. We don't support animations etc.
			CHECKLENGTH(6);
			ADVANCE(1); // skip blocksize 
			helper = *(byte*)buffer; // flags
			ADVANCE(3); // advance flags and skip delayTime
			if (helper & 1) {
				transparentColorIndex = *(byte*)buffer;
			}
			ADVANCE(2); // advance colorIndex and skip terminator
			break;
		case 0xFE: // Comment extension 
			helper = 1;
			while (helper) {
				helper = *(byte*)buffer; // bytes in block
				CHECKLENGTH((size_t)helper+1); ADVANCE((size_t)helper+1);
			}
			break;
		case 0xFF: // Application extension (XMP data or random shit)
			CHECKLENGTH(12); ADVANCE(12);
			helper = 1;
			while (helper) {
				helper = *(byte*)buffer; // bytes in block
				CHECKLENGTH((size_t)helper+1); ADVANCE((size_t)helper+1);
			}
			break;

		}
		
		CHECKLENGTH(1); type = *(byte*)buffer;	ADVANCE(1);
	}

	if (type != 0x2C) {
		return;
	}
	
	// the actual image
	CHECKLENGTH(sizeof(gifLocalImage_t));
	gifLocalImage_t*	localImage = (gifLocalImage_t*)buffer;
	ADVANCE(sizeof(gifLocalImage_t)-1); // -1 cuz struct alignment. SIGH

	if (localImage->flags & GIFLOCALIMAGEFLAG_LCT) {
		return;
		// untested, don't do it for now
		/*lctLen = 1 << ((localImage->flags & GIFLOCALIMAGEFLAG_SIZEMASK) + 1);
		if (lctLen * sizeof(colorvec_t) >= len) {
			return;
		}
		lct = new colorvec_t[lctLen];
		memcpy(lct, buffer, lctLen * sizeof(colorvec_t));
		ADVANCE(lctLen * sizeof(colorvec_t) - 1); // -1 because the meaningful data is actually 13 bytes but struct auto aligns due to the uint16_t
		*/
	}

	if (lct) {
		delete[] lct;
	}
	if (gct) {
		delete[] gct;
	}

}

extern "C"  __declspec(noinline) __declspec(dllexport) int loadfile(const char* file) {
	FILE* f = NULL;
	std::cout << file << "\n";
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
		read_gif(buffer, len);
		delete[] buffer;
	}
	return 0;
}

int main(int argc, char* argv[])
{
	loadfile(argv[1]);
}
