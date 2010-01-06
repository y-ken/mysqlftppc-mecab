#include "reader_mecab.h"

MecabReader::MecabReader(FtCharReader *feeder, MeCab::Tagger *tagger, CHARSET_INFO *dictionary_charset){
	this->feeder = feeder;
	feeder_feed = true;
	this->tagger = tagger;
	node = NULL;
	input = new FtMemBuffer(dictionary_charset);
	output = NULL;
	this->dictionary_charset = dictionary_charset;
	wc_sp = false;
	bypass = false;
}

MecabReader::~MecabReader(){
	delete input;
	if(output){
		delete output;
	}
}

bool MecabReader::readOne(my_wc_t *wc, int *meta){
	if(output){
		if(output->readOne(wc, meta)){
			return true;
		}
		delete output;
		output = NULL;
	}
	
	if(node && node->next){
		node = node->next;
	}else{
		if(wc_sp){
			wc_sp = false;
			*wc = wc_in;
			*meta = meta_in;
			return true;
		}
		if(!feeder_feed){
			return false;
		}
		if(bypass){
			return feeder->readOne(wc, meta);
		}
		input->reset();
		while(feeder_feed = feeder->readOne(&wc_in, &meta_in)){
//			fprintf(stderr,"mecab got %lu %d\n", wc_in, meta_in); fflush(stderr);
			if(meta_in == FT_CHAR_NORM){
				input->append(wc_in);
			}else{
				wc_sp = true;
				break;
			}
		}
		input->flush();
		
		size_t length;
		size_t capacity;
		char *buffer=input->getBuffer(&length, &capacity);
		if(length > 0){
			node = tagger->parseToNode(const_cast<char*>(buffer), length);
		}else{
			node = NULL;
		}
	}
	if(node){
		if(node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE){
			*wc = FT_EOS;
			*meta = FT_CHAR_CTRL;
			return true;
		}else{
			output = new FtMemReader(node->surface, node->length, this->dictionary_charset);
		}
	}
	return readOne(wc, meta);
}

void MecabReader::reset(){
	feeder->reset();
	node = NULL;
	if(output){
		delete output;
	}
	output = NULL;
}
