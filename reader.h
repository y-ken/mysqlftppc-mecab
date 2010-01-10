#ifndef _FT_READER
#include "mysqldep.h"

#define FT_EOS 0xFFFF

#define FT_CHAR_NORM   0x000
#define FT_CHAR_CTRL   0x001
#define FT_CHAR_QUOT   0x002
#define FT_CHAR_LEFT   0x004
#define FT_CHAR_RIGHT  0x008
#define FT_CHAR_YES    0x010
#define FT_CHAR_NO     0x020
#define FT_CHAR_STRONG 0x040
#define FT_CHAR_WEAK   0x080
#define FT_CHAR_NEG    0x100
#define FT_CHAR_TRUNC  0x200
/**
   +a (c "de")
   makes
   CTRL|YES
   NORM
   CTRL
   CTRL|LEFT
   NORM
   CTRL
   CTRL|QUOT|LEFT
   NORM
   NORM
   CTRL|QUOT|RIGHT
   CTRL|RIGHT
*/

class FtCharReader {
public:
	virtual ~FtCharReader(){};
	virtual bool readOne(my_wc_t *wc, int *meta) = 0;
	virtual void reset() = 0;
};

class FtMemReader : public FtCharReader {
	const char* directBuffer;
	size_t directBufferLength;
	CHARSET_INFO* cs;
	const char* cursor;
public:
	FtMemReader(const char* buffer, size_t bufferLength, CHARSET_INFO *cs);
	~FtMemReader();
	bool readOne(my_wc_t *wc, int *meta);
	void reset();
};

enum FtLineStyle {
	FT_LINE_IGNORE,
	FT_LINE_EOL,
	FT_LINE_DOUBLE,
};

class FtLineReader : public FtCharReader {
	my_wc_t wcs[3];
	int metas[3];
	size_t wcs_fill;
	enum FtLineStyle lineStyle;
	FtCharReader *feeder;
	bool feeder_feed;
public:
	FtLineReader(FtCharReader *feeder);
	~FtLineReader();
	bool readOne(my_wc_t *wc, int *meta);
	void reset();
	// 
	void setLineStyle(enum FtLineStyle style);
};

class FtBreakReader : public FtCharReader {
	FtCharReader *feeder;
	CHARSET_INFO *cs;
public:
	FtBreakReader(FtCharReader *feeder, CHARSET_INFO *cs);
	~FtBreakReader();
	bool readOne(my_wc_t *wc, int *meta);
	void reset();
};

class FtBoolReader : public FtCharReader {
	FtCharReader *src;
	bool strhead;
	bool quot;
public:
	FtBoolReader(FtCharReader *feed);
	~FtBoolReader();
	bool readOne(my_wc_t *wc, int *meta);
	void reset();
};

#define _FT_READER 1
#endif // _FT_READER
