#include "reader.h"

enum FtNormalization {
	FT_NORM_OFF,
	FT_NORM_NFC,
	FT_NORM_NFD,
	FT_NORM_NFKC,
	FT_NORM_NFKD,
	FT_NORM_FCD
};

#if HAVE_ICU
#include <unicode/chariter.h>
#include <unicode/normlzr.h>
#include <unicode/schriter.h>
#include <unicode/brkiter.h>

// initial capacity may be the other value.
#define FT_CODEPOINTS_CAPACITY 32

class WcIterator : public CharacterIterator {
	FtCharReader *feed;
	
	// mirrored cache
	UnicodeString *cache;
	StringCharacterIterator *cacheIterator;
	int control; // control char index
	int controlLength;
	int metas[FT_CODEPOINTS_CAPACITY]; // metainfo for control chars
	size_t formerLength;
	size_t former32Length;
	bool eoc; // End Of Cache (reached final cache)
	void mirror();
	// UChar32 version of textLength
	int32_t textLength32;
public:
	WcIterator(FtCharReader *internal);
	WcIterator(FtCharReader *internal, int32_t internalLength, int32_t internalLength32);
	~WcIterator();
	// FowardCharacterIterator
	UBool operator== (const ForwardCharacterIterator &that) const;
	int32_t hashCode() const;
	virtual UClassID getDynamicClassID(void) const { return 0; };
	UChar nextPostInc();
	UChar32 next32PostInc();
	UBool hasNext();
	// CharacterIterator
	CharacterIterator* clone() const;
	UChar first();
	UChar firstPostInc();
	UChar32 first32();
	UChar32 first32PostInc();
	UChar last();
	UChar32 last32();
	UChar setIndex(int32_t position);
	UChar32 setIndex32(int32_t position);
	UChar current() const;
	UChar32 current32() const;
	UChar next();
	UChar32 next32();
	UChar previous();
	UChar32 previous32();
	UBool hasPrevious();
	int32_t move(int32_t delta, EOrigin origin);
	int32_t move32(int32_t delta, EOrigin origin);
	void getText(UnicodeString &result);
	// WcIterator
	void reset();
	int getPreviousControlMeta();
};

class FtUnicodeNormalizerReader : public FtCharReader {
	WcIterator *wrapper;
	Normalizer *normalizer;
	UNormalizationMode mode;
	bool eos;
public:
	FtUnicodeNormalizerReader(FtCharReader *internal, UNormalizationMode mode);
	~FtUnicodeNormalizerReader();
	bool readOne(my_wc_t *wc, int *meta);
	void reset();
	//
	void setOption(int32_t option, UBool value);
};

class FtUnicodeBreakReader : public FtCharReader {
	WcIterator *wrapper;
	BreakIterator *breaker;
	bool feeder_feed;
	bool wc_sp;
public:
	FtUnicodeBreakReader(FtCharReader *feeder, const char* locale);
	~FtUnicodeBreakReader();
	bool readOne(my_wc_t *wc, int *meta);
	void reset();
};

#endif
