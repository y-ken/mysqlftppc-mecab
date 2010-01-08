#include <cstring>
#include <cstdio>
#include "plugin_mecab.h"
#include "reader_mecab.h"
#include "ctype-utf16.h"
#if HAVE_ICU
#include <unicode/uversion.h>
#include <unicode/uchar.h>
#include <unicode/uclean.h>
#include <unicode/unorm.h>
#endif
#include <my_sys.h>

#if !defined(__attribute__) && (defined(__cplusplus) || !defined(__GNUC__)  || __GNUC__ == 2 && __GNUC_MINOR__ < 8)
#define __attribute__(A)
#endif

static void  icu_free(const void* context, void *ptr){ my_free(ptr,MYF(0)); }
static void* icu_malloc(const void* context, size_t size){ return my_malloc(size,MYF(MY_WME)); }
static void* icu_realloc(const void* context, void* ptr, size_t size){
	if(ptr!=NULL) return my_realloc(ptr,size,MYF(MY_WME));
	return my_malloc(size,MYF(MY_WME));
}

static int mecab_parser_plugin_init(void *arg __attribute__((unused))){
	strcat(mecab_info, "with mecab ");
	strcat(mecab_info, mecab_version());
#if HAVE_ICU
	char icu_tmp_str[16];
	char errstr[128];
	UVersionInfo versionInfo;
	u_getVersion(versionInfo); // get ICU version
	u_versionToString(versionInfo, icu_tmp_str);
	strcat(mecab_info, ", ICU ");
	strcat(mecab_info, icu_tmp_str);
	u_getUnicodeVersion(versionInfo); // get ICU Unicode version
	u_versionToString(versionInfo, icu_tmp_str);
	strcat(mecab_info, "(Unicode ");
	strcat(mecab_info, icu_tmp_str);
	strcat(mecab_info, ")");

	UErrorCode ustatus=U_ZERO_ERROR;
	u_setMemoryFunctions(NULL, icu_malloc, icu_realloc, icu_free, &ustatus);
	if(U_FAILURE(ustatus)){
		sprintf(errstr, "u_setMemoryFunctions failed. ICU status code %d\n", ustatus);
		fputs(errstr, stderr);
		fflush(stderr);
	}
#else
	strcat(mecab_info, ", without ICU");
#endif
	
	{
		int argc=0;
		const char* argv[4];
		if(strlen(mecab_dicdir)>0){
			argv[argc]="-d";
			argc++;
			argv[argc]=mecab_dicdir;
			argc++;
		}
		if(strlen(mecab_userdic)>0){
			argv[argc]="-u";
			argc++;
			argv[argc]=mecab_userdic;
			argc++;
		}
		MeCab::Tagger *tmp = MeCab::createTagger(argc, (char**)argv);
		if(tmp){
			delete tmp;
		}else{
			fprintf(stderr, "mecab plugin load error (dictionary is missing?)\n"); fflush(stderr);
			return -1;
		}
	}
	return 0;
}

static int mecab_parser_plugin_deinit(void *arg __attribute__((unused))){
	return 0;
}

static int mecab_parser_init(MYSQL_FTPARSER_PARAM *param __attribute__((unused))){
	param->ftparser_state = new FtMecabState();
	return 0;
}

static int mecab_parser_deinit(MYSQL_FTPARSER_PARAM *param __attribute__((unused))){
	delete (FtMecabState*)(param->ftparser_state);
	return 0;
}

/*
void dumpInfo(MYSQL_FTPARSER_BOOLEAN_INFO *info){
	char a = '_';
	if(info->quot){ a=*(info->quot); }
	fprintf(stderr, "parser_info type:%d yesno:%d weight_adjust:%d wasign:%d trunc:%d byte:%c quot:%c\n",
			info->type, info->yesno, info->weight_adjust, info->wasign, info->trunc, info->prev, a);
	fflush(stderr);
}
void dumpChars(const char *head, size_t length){
	int i;
	for(i=0;i<length;i++){
		fprintf(stderr,"%x ",(unsigned char)head[i]);
	}
	fprintf(stderr,"\n");
	fflush(stderr);
}
*/

static void pooled_add_word(FtMemBuffer *membuffer, FtMemPool *pool, MYSQL_FTPARSER_PARAM *param, MYSQL_FTPARSER_BOOLEAN_INFO *info){
	size_t length;
	size_t capacity;
	char *binary = membuffer->getBuffer(&length, &capacity);
	
	if(length > 0){
		info->type = FT_TOKEN_WORD;
		const char *save = pool->findPool(param->doc, param->length, const_cast<char*>(binary), length);
		if(save){
			if(info){ param->mysql_add_word(param, (char*)save, length, info); }
		}else{
			membuffer->detach();
			if(info){ param->mysql_add_word(param, binary, length, info); }
			pool->addHeap(new FtMemHeap(binary, length, capacity));
		}
	}
}

static int mecab_parser_parse(MYSQL_FTPARSER_PARAM *param){
	DBUG_ENTER("mecab_parser_parse");
//fprintf(stderr,"input:  ");
//dumpChars(const_cast<char*>(param->doc), (size_t)param->length);
	FtMecabState *state = (FtMecabState*)(param->ftparser_state);
	FtCharReader *reader;
	FtMemReader memReader(const_cast<char*>(param->doc), (size_t)param->length, param->cs);
	reader = &memReader;
	
	MYSQL_FTPARSER_BOOLEAN_INFO info = { FT_TOKEN_WORD, 1, 0, 0, 0, ' ', 0 };
	
	if(param->mode == MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
		FtBoolReader boolParser(reader);
		reader = &boolParser;
		
		FtLineReader lineReader(reader);
		lineReader.setLineStyle(state->eolstyle);
		reader = &lineReader;
		
		MecabReader mecabReader(reader, state->engine, state->engine_charset);
		mecabReader.bypass = true;
		reader = &mecabReader;
		
#if HAVE_ICU
		// post-normalizer
		FtUnicodeNormalizerReader *normReader = NULL;
		if(state->normalization != FT_NORM_OFF){
			if(state->normalization == FT_NORM_NFC){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFC);
			}else if(state->normalization == FT_NORM_NFD){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFD);
			}else if(state->normalization == FT_NORM_NFKC){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFKC);
			}else if(state->normalization == FT_NORM_NFKD){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFKD);
			}else if(state->normalization == FT_NORM_FCD){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_FCD);
			}
			if(state->unicode_v32){
				normReader->setOption(UNORM_UNICODE_3_2, TRUE);
			}
			reader = normReader;
		}
#endif
		
		FtMemBuffer memBuffer(param->cs);
		
		char dummy = '\"';
		my_wc_t wc;
		int meta;
		while(reader->readOne(&wc, &meta)){
//			fprintf(stderr,"final got %lu %d\n", wc, meta); fflush(stderr);
			if(meta==FT_CHAR_NORM){
				memBuffer.append(wc);
			}else{
				memBuffer.flush();
				pooled_add_word(&memBuffer, state->pool, param, &info);
				memBuffer.reset();
				
				if(meta==FT_CHAR_CTRL){       info.yesno = 0; }
				else if(meta&FT_CHAR_YES){    info.yesno = +1; }
				else if(meta&FT_CHAR_NO){     info.yesno = -1; }
				else if(meta&FT_CHAR_STRONG){ info.weight_adjust++; }
				else if(meta&FT_CHAR_WEAK){   info.weight_adjust--; }
				else if(meta&FT_CHAR_NEG){    info.wasign = !info.wasign; }
				else if(meta&FT_CHAR_TRUNC){  info.trunc = 1; }
				
				if(meta&FT_CHAR_LEFT){
					info.type = FT_TOKEN_LEFT_PAREN;
					if(meta&FT_CHAR_QUOT){
						info.quot = &dummy;
					}
					param->mysql_add_word(param, NULL, 0, &info);
					info.type = FT_TOKEN_WORD;
					mecabReader.bypass = false;
				}else if(meta&FT_CHAR_RIGHT){
					info.type = FT_TOKEN_RIGHT_PAREN;
					param->mysql_add_word(param, NULL, 0, &info);
					if(meta&FT_CHAR_QUOT){
						info.quot = NULL;
					}
					info.type = FT_TOKEN_WORD;
					mecabReader.bypass = true;
				}
			}
		}
		memBuffer.flush();
		pooled_add_word(&memBuffer, state->pool, param, &info);
#if HAVE_ICU
		if(normReader){
			delete normReader;
		}
#endif
	}else{
		FtLineReader lineReader(reader);
		lineReader.setLineStyle(state->eolstyle);
		reader = &lineReader;
		
		MecabReader mecabReader(reader, state->engine, state->engine_charset);
		reader = &mecabReader;
		
#if HAVE_ICU
		FtUnicodeNormalizerReader *normReader = NULL;
		if(state->normalization != FT_NORM_OFF){
			if(state->normalization == FT_NORM_NFC){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFC);
			}else if(state->normalization == FT_NORM_NFD){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFD);
			}else if(state->normalization == FT_NORM_NFKC){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFKC);
			}else if(state->normalization == FT_NORM_NFKD){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_NFKD);
			}else if(state->normalization == FT_NORM_FCD){
				normReader = new FtUnicodeNormalizerReader(reader, UNORM_FCD);
			}
			if(state->unicode_v32){
				normReader->setOption(UNORM_UNICODE_3_2, TRUE);
			}
			reader = normReader;
		}
#endif
		FtMemBuffer memBuffer(param->cs);
		
		my_wc_t wc;
		int meta;
		while(reader->readOne(&wc, &meta)){
			if(meta==FT_CHAR_NORM){
				memBuffer.append(wc);
			}else{
				memBuffer.flush();
				pooled_add_word(&memBuffer, state->pool, param, &info);
				memBuffer.reset();
			}
		}
		memBuffer.flush();
		pooled_add_word(&memBuffer, state->pool, param, &info);
#if HAVE_ICU
		if(normReader){
			delete normReader;
		}
#endif
	}
	DBUG_RETURN(0);
}

FtMecabState::FtMecabState(){
	pool = new FtMemPool();
	
	engine = NULL;
	{
		int argc=0;
		const char* argv[4];
		if(strlen(mecab_dicdir)>0){
			argv[argc]="-d";
			argc++;
			argv[argc]=mecab_dicdir;
			argc++;
		}
		if(strlen(mecab_userdic)>0){
			argv[argc]="-u";
			argc++;
			argv[argc]=mecab_userdic;
			argc++;
		}
		engine = MeCab::createTagger(argc, (char**)argv);
	}
	
	engine_charset = NULL;
	{
		const char* dicCharset = engine->dictionary_info()->charset;
		CHARSET_INFO* cs = NULL;
		if(strcasecmp(dicCharset, "UTF-8") || strcasecmp(dicCharset, "utf8") || strcasecmp(dicCharset, "utf_8")){
			cs = get_charset(33, MYF(0)); // MySQL utf8 (utf8_general_ci)
		}else if(strcasecmp(dicCharset, "EUC-JP") || strcasecmp(dicCharset, "euc") || strcasecmp(dicCharset, "euc_jp")){
			cs = get_charset(97, MYF(0)); // MySQL eucjpms (my_charset_eucjpms_japanese_ci)
			if(!cs){
				cs=get_charset(91, MYF(0));  // MySQL ujis
			}
		}else if(strcasecmp(dicCharset, "CP932") || strcasecmp(dicCharset, "Shift_JIS") || strcasecmp(dicCharset, "shift-jis") || strcasecmp(dicCharset, "sjis")){
			cs = get_charset(96, MYF(0)); // MySQL cp932
			if(!cs){
				cs = get_charset(88, MYF(0)); // MySQL sjis
			}
		}else if(strcasecmp(dicCharset, "ascii")){
			cs = get_charset(65, MYF(0)); // MySQL ascii
		}else if(strcasecmp(dicCharset, "UTF-16BE") || strcasecmp(dicCharset, "utf16be") || strcasecmp(dicCharset, "utf_16be")){
			cs = get_ft_charset_utf16be_general_ci();
		}else if(strcasecmp(dicCharset, "UTF-16LE") || strcasecmp(dicCharset, "utf16le") || strcasecmp(dicCharset, "utf_16le")){
			cs = get_ft_charset_utf16le_general_ci();
		}else if(strcasecmp(dicCharset, "UTF-16") || strcasecmp(dicCharset, "utf16") || strcasecmp(dicCharset, "utf_16")){
			// UTF-16 without BOM is UTF-16BE
			cs = get_ft_charset_utf16be_general_ci();
		}
		if(!cs){
			cs = get_charset(33, MYF(0)); // MySQL utf8
		}
		engine_charset = cs;
	}
	
	eolstyle = FT_LINE_DOUBLE;
	if(mecab_eolstyle){
		if(strcasecmp(mecab_eolstyle, "DOUBLE")==0){
			eolstyle = FT_LINE_DOUBLE;
		}else if(strcasecmp(mecab_eolstyle, "EOL")==0){
			eolstyle = FT_LINE_EOL;
		}else if(strcasecmp(mecab_eolstyle, "IGNORE")==0){
			eolstyle = FT_LINE_IGNORE;
		}
	}
	
	normalization = FT_NORM_OFF;
	if(mecab_normalization){
		if(strcmp(mecab_normalization, "OFF")==0){
			normalization = FT_NORM_OFF;
		}else if(strcmp(mecab_normalization, "KC")==0){
			normalization = FT_NORM_NFKC;
		}else if(strcmp(mecab_normalization, "KD")==0){
			normalization = FT_NORM_NFKD;
		}else if(strcmp(mecab_normalization, "C")==0){
			normalization = FT_NORM_NFC;
		}else if(strcmp(mecab_normalization, "D")==0){
			normalization = FT_NORM_NFD;
		}else if(strcmp(mecab_normalization, "FCD")==0){
			normalization = FT_NORM_FCD;
		}
	}
	
	unicode_v32 = false;
	if(mecab_unicode_version && strcmp(mecab_unicode_version, "3.2")==0){
		unicode_v32 = true;
	}
}

FtMecabState::~FtMecabState(){
	delete pool;
	delete engine;
}

static int mecab_dicdir_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
	char buf[1024];
	int len=1024;
	const char *str;
	
	str = value->val_str(value,buf,&len);
	if(!str) return -1;
	*(const char**)save=str;
	
	{
		int argc=0;
		const char* argv[4];
		if(strlen(str)>0){
			argv[argc]="-d";
			argc++;
			argv[argc]=str;
			argc++;
		}
		if(strlen(mecab_userdic)>0){
			argv[argc]="-u";
			argc++;
			argv[argc]=mecab_userdic;
			argc++;
		}
		MeCab::Tagger *engine = MeCab::createTagger(argc, (char**)argv);
		if(engine){
			delete engine;
		}else{
			return -1;
		}
	}
	return 0;
}

static int mecab_userdic_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
	char buf[1024];
	int len=1024;
	const char *str;
	
	str = value->val_str(value,buf,&len);
	if(!str) return -1;
	*(const char**)save=str;
	
	{
		int argc=0;
		const char* argv[4];
		if(strlen(mecab_dicdir)>0){
			argv[argc]="-d";
			argc++;
			argv[argc]=mecab_dicdir;
			argc++;
		}
		if(strlen(str)>0){
			argv[argc]="-u";
			argc++;
			argv[argc]=str;
			argc++;
		}
		MeCab::Tagger *engine = MeCab::createTagger(argc, (char**)argv);
		if(engine){
			delete engine;
		}else{
			return -1;
		}
	}
	return 0;
}

static int mecab_eolstyle_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
	char buf[8];
	int len=8;
	const char *str;
	
	str = value->val_str(value, buf, &len);
	if(!str){ return -1; }
	*(const char**)save = str;
	
	if(len==6){
		if(strncasecmp(str, "DOUBLE", len)==0 || strncasecmp(str, "IGNORE", len)==0){
			return 0;
		}
	}
	if(len==3){
		if(strncasecmp(str, "EOL", len)==0){
			return 0;
		}
	}
	return -1;
}

static int mecab_unicode_version_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
	char buf[4];
	int len=4;
	const char *str;
	
	str = value->val_str(value,buf,&len);
	if(!str){ return -1; }
	*(const char**)save=str;
	if(len==3){
		if(memcmp(str, "3.2", len)==0){ return 0; }
	}
	if(len==7){
		if(memcmp(str, "DEFAULT", len)==0){ return 0; }
	}
	return -1;
}

static int mecab_normalization_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
	char buf[4];
	int len=4;
	const char *str;
	
	str = value->val_str(value,buf,&len);
	if(!str) return -1;
	*(const char**)save=str;
	if(len==1){
		if(str[0]=='C'){ return 0; }
		if(str[0]=='D'){ return 0; }
	}else if(len==2){
		if(str[0]=='K' && str[1]=='C'){ return 0; }
		if(str[0]=='K' && str[1]=='D'){ return 0; }
	}else if(len==3){
		if(str[0]=='F' && str[1]=='C' && str[2]=='D'){ return 0; }
		if(str[0]=='O' && str[1]=='F' && str[2]=='F'){ return 0; }
	}
	return -1;
}

