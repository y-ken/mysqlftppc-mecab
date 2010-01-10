#include "reader.h"

// #include <cstdio>

//////////////// FtMemReader
FtMemReader::FtMemReader(const char* buffer, std::size_t bufferLength, CHARSET_INFO *cs){
	cursor = directBuffer = buffer;
	directBufferLength =bufferLength;
	this->cs = cs;
}

FtMemReader::~FtMemReader(){}

bool FtMemReader::readOne(my_wc_t *wc, int *meta){
	if(cursor >= directBuffer+directBufferLength){
		*wc = FT_EOS;
		*meta = FT_CHAR_CTRL;
		return FALSE;
	}
	*meta = FT_CHAR_NORM;
	int cnvres=cs->cset->mb_wc(cs, wc, (uchar*)cursor, (uchar*)(directBuffer+directBufferLength));
	if(cnvres > 0){
		cursor+=cnvres;
	}else{
		cursor++;
		*wc='?';
	}
	return true;
}

void FtMemReader::reset(){
	cursor = directBuffer;
}

//////////////// FtLineReader
FtLineReader::FtLineReader(FtCharReader *feeder){
	this->feeder = feeder;
	feeder_feed = true;
	wcs_fill = 0;
}

FtLineReader::~FtLineReader(){}

bool FtLineReader::readOne(my_wc_t *wc, int *meta){
	while(wcs_fill<3 && feeder_feed){
		my_wc_t wc_in;
		int meta_in;
		if(feeder_feed = feeder->readOne(&wc_in, &meta_in)){
//			fprintf(stderr,"line got %lu %d\n", wc_in, meta_in); fflush(stderr);
			wcs[0] = wcs[1];
			wcs[1] = wcs[2];
			wcs[2] = wc_in;
			metas[0] = metas[1];
			metas[1] = metas[2];
			metas[2] = meta_in;
			wcs_fill++;
			
			if(lineStyle == FT_LINE_IGNORE){
				if(wcs[2]==0xD || wcs[2]==0xA){
					wcs[1]=wcs[0];
					metas[1]=metas[0];
					wcs_fill--;
				}
			}else{ // if(lineStyle == FT_LINE_EOL || lineStyle == FT_LINE_DOUBLE){
				if(wcs[1]==0xD){
					if(wcs[2]==0xA){
						wcs[1]=wcs[0];
						metas[1]=metas[0];
						wcs_fill--;
					}else{
						wcs[1]=0xA;
					}
				}
			}
		}
	}
	
	if(lineStyle == FT_LINE_EOL){
		int read_pos = 3-wcs_fill;
		if(wcs_fill>0 && wcs[read_pos]==0xA){
			wcs[read_pos] = FT_EOS;
			meta[read_pos] = FT_CHAR_CTRL;
		}
	}else if(lineStyle == FT_LINE_DOUBLE){
		int read_pos = 3-wcs_fill;
		if(wcs[read_pos]==0xA && metas[read_pos]==FT_CHAR_NORM){
			if(read_pos<2 && wcs[read_pos+1]==0xA){
				wcs[read_pos+1] = FT_EOS;
				metas[read_pos+1] = FT_CHAR_CTRL;
			}
			wcs_fill--;
		}
	}
	if(wcs_fill>0){
		int read_pos = 3-wcs_fill;
		*wc = wcs[read_pos];
		*meta = metas[read_pos];
		wcs_fill--;
		return true;
	}else{
		*wc = FT_EOS;
		*meta = FT_CHAR_CTRL;
		return false;
	}
}

void FtLineReader::reset(){
	feeder->reset();
	feeder_feed = true;
	wcs_fill = 0;
}

void FtLineReader::setLineStyle(enum FtLineStyle style){
	lineStyle = style;
}


//////////////// FtBreakReader
#include <m_ctype.h>
FtBreakReader::FtBreakReader(FtCharReader *feeder, CHARSET_INFO *cs){
	this->feeder = feeder;
	this->cs = cs;
}

FtBreakReader::~FtBreakReader(){}

bool FtBreakReader::readOne(my_wc_t *wc, int *meta){
	if(feeder->readOne(wc, meta)){
		my_wc_t wwc = *wc;
		if(*meta == FT_CHAR_NORM && wwc!='_'){
			int ctype = my_uni_ctype[wwc>>8].ctype ? my_uni_ctype[wwc>>8].ctype[wwc&0xFF] : my_uni_ctype[wwc>>8].pctype;
			if(ctype & (_MY_U | _MY_L | _MY_NMR)){
				// FT_CHAR_NORM
			}else{
				*meta = FT_CHAR_CTRL;
			}
		}
		return true;
	}
	return false;
}

void FtBreakReader::reset(){
	feeder->reset();
}


//////////////// FtBoolReader
FtBoolReader::FtBoolReader(FtCharReader *feed){
	strhead = true;
	quot = false;
	src = feed;
}

FtBoolReader::~FtBoolReader(){}

bool FtBoolReader::readOne(my_wc_t *wc, int *meta){
	bool ret = src->readOne(wc, meta);
	if(*wc=='\\' && *meta==FT_CHAR_NORM){
		if(ret = src->readOne(wc, meta)){
			*meta = FT_CHAR_NORM;
			strhead = false;
			return true;
		}
	}
	if(ret==false){
		return false;
	}
	
	if(*meta==FT_CHAR_NORM){
		if(this->quot){
			if(*wc=='"'){
				this->quot=false;
				*meta = FT_CHAR_CTRL|FT_CHAR_QUOT|FT_CHAR_RIGHT;
			}
		}else if(*wc=='"'){
			this->quot=true;
			*meta = FT_CHAR_CTRL|FT_CHAR_QUOT|FT_CHAR_LEFT;
		}else{
			if(*wc=='('){
				*meta = FT_CHAR_CTRL|FT_CHAR_LEFT;
			}else if(*wc==')'){
				*meta = FT_CHAR_CTRL|FT_CHAR_RIGHT;
			}else{
				if(*wc==' '){ *meta = FT_CHAR_CTRL; }
				if(strhead){
					if(*wc=='+'){ *meta = FT_CHAR_CTRL|FT_CHAR_YES; }
					if(*wc=='-'){ *meta = FT_CHAR_CTRL|FT_CHAR_NO; }
					if(*wc=='>'){ *meta = FT_CHAR_CTRL|FT_CHAR_STRONG; }
					if(*wc=='<'){ *meta = FT_CHAR_CTRL|FT_CHAR_WEAK; }
					if(*wc=='!'){ *meta = FT_CHAR_CTRL|FT_CHAR_NEG; }
				}else{
					if(*wc=='*'){ *meta = FT_CHAR_CTRL|FT_CHAR_TRUNC; }
				}
			}
		}
	}
	if(*meta==FT_CHAR_NORM){
		strhead = false;
	}else{
		strhead = true;
	}
	
	return true;
}

void FtBoolReader::reset(){
	strhead = true;
	quot = false;
	src->reset();
}
