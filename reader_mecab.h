#include <mecab.h>
#include "reader.h"
#include "buffer.h"

class MecabReader : public FtCharReader {
	FtCharReader *feeder;
	bool feeder_feed;
	MeCab::Tagger *tagger;
	const MeCab::Node *node;
	FtMemBuffer *input;
	FtMemReader *output;
	bool wc_sp;
	my_wc_t wc_in;
	int meta_in;
	CHARSET_INFO *dictionary_charset;
public:
	MecabReader(FtCharReader *feeder, MeCab::Tagger *tagger, CHARSET_INFO *dictionary_charset);
	~MecabReader();
	bool readOne(my_wc_t *wc, int *meta);
	void reset();
	// 
	bool bypass;
};

