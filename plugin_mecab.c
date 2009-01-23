#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <mecab.h>

#include "ftbool.h"
#include "ftstring.h"
#if HAVE_ICU
#include <unicode/uclean.h>
#include <unicode/uversion.h>
#include <unicode/uchar.h>
#include <unicode/unorm.h>
#include "ftnorm.h"
#endif

// mysql headers
#include <my_global.h>
#include <m_ctype.h>
#include <my_sys.h>
#include <my_list.h>
#include <plugin.h>

#define HA_FT_MAXBYTELEN 254
#define FTPPC_MEMORY_ERROR -1
#define FTPPC_NORMALIZATION_ERROR -2

#if !defined(__attribute__) && (defined(__cplusplus) || !defined(__GNUC__)  || __GNUC__ == 2 && __GNUC_MINOR__ < 8)
#define __attribute__(A)
#endif

static char* mecab_unicode_normalize;
static char* mecab_unicode_version;
static char  mecab_info[128];
static char* mecab_dicdir;
static char* mecab_userdic;

static void* icu_malloc(const void* context, size_t size){ return my_malloc(size,MYF(MY_WME)); }
static void* icu_realloc(const void* context, void* ptr, size_t size){ return my_realloc(ptr,size,MYF(MY_WME)); }
static void  icu_free(const void* context, void *ptr){ my_free(ptr,MYF(0)); }


/** ftstate */
static LIST* list_top(LIST* root){
  LIST *cur = root;
  while(cur && cur->next){
    cur = cur->next;
  }
  return cur;
}

struct ftppc_mem_bulk {
  void*  mem_head;
  void*  mem_cur;
  size_t mem_size;
};

struct ftppc_state {
  /** immutable memory buffer */
  size_t bulksize;
  LIST*  mem_root;
  void*  engine;
  CHARSET_INFO* engine_charset;
};

static void* ftppc_alloc(struct ftppc_state *state, size_t length){
  LIST *cur = list_top(state->mem_root);
  while(1){
    if(!cur){
      // hit the root. create a new bulk.
      size_t sz = state->bulksize<<1;
      while(sz < length){
        sz = sz<<1;
      }
      state->bulksize = sz;
      
      struct ftppc_mem_bulk *tmp = (struct ftppc_mem_bulk*)my_malloc(sizeof(struct ftppc_mem_bulk), MYF(MY_WME));
      tmp->mem_head = my_malloc(sz, MYF(MY_WME));
      tmp->mem_cur  = tmp->mem_head;
      tmp->mem_size = sz;
      
      if(!tmp->mem_head){ return NULL; }
      
      state->mem_root = list_cons(tmp, cur);
      cur = state->mem_root;
    }
    
    struct ftppc_mem_bulk *bulk = (struct ftppc_mem_bulk*)cur->data;
    
    if(bulk->mem_cur + length < bulk->mem_head + bulk->mem_size){
      void* addr = bulk->mem_cur;
      bulk->mem_cur += length;
      return addr;
    }
    cur = cur->prev;
  }
}
/** /ftstate */


static int mecab_parser_plugin_init(void *arg __attribute__((unused)))
{
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
  
  UErrorCode ustatus=0;
  u_setMemoryFunctions(NULL, icu_malloc, icu_realloc, icu_free, &ustatus);
  if(U_FAILURE(ustatus)){
    sprintf(errstr, "u_setMemoryFunctions failed. ICU status code %d\n", ustatus);
    fputs(errstr, stderr);
    fflush(stderr);
  }
#else
  strcat(mecab_info, ", without ICU");
#endif
  return(0);
}
static int mecab_parser_plugin_deinit(void *arg __attribute__((unused)))
{
  return(0);
}


static int mecab_parser_init(MYSQL_FTPARSER_PARAM *param __attribute__((unused)))
{
  // mecab arguments
  int argc=0;
  char* argv[4];
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
  mecab_t *mecab = mecab_new(argc, argv);
  const mecab_dictionary_info_t *dic = mecab_dictionary_info(mecab);
  /* We only have to check the first dictionary(system dictionary)  
     because dic->next(user dictionary) will have the same charset. */
  CHARSET_INFO* cs = NULL; // MySQL utf8
  if(strcasecmp(dic->charset, "utf8") || strcasecmp(dic->charset, "utf_8") || strcasecmp(dic->charset, "utf-8")){
    cs=get_charset(33, MYF(0));
  }else if(strcasecmp(dic->charset, "euc") || strcasecmp(dic->charset, "euc_jp") || strcasecmp(dic->charset, "euc-jp")){
    cs=get_charset(98, MYF(0)); // MySQL eucjpms
    if(!cs){
      cs=get_charset(91, MYF(0));  // MySQL ujis
    }
  }else if(strcasecmp(dic->charset, "sjis") || strcasecmp(dic->charset, "shift-jis") 
      || strcasecmp(dic->charset, "shift_jis") || strcasecmp(dic->charset, "cp932")){
    cs=get_charset(96, MYF(0)); // MySQL cp932
    if(!cs){
      cs=get_charset(88, MYF(0)); // MySQL sjis
    }
  }else if(strcasecmp(dic->charset, "ascii")){
    cs=get_charset(65, MYF(0)); // MySQL ascii
  }else{
    cs=get_charset(33, MYF(0)); // MySQL utf8
  }
  
  struct ftppc_state *state = (struct ftppc_state*)my_malloc(sizeof(struct ftppc_state), MYF(MY_WME));
  state->bulksize = 8;
  state->mem_root = NULL;
  state->engine   = mecab;
  state->engine_charset = cs;
  param->ftparser_state = state;
  
  return(0);
}

static int mecab_parser_deinit(MYSQL_FTPARSER_PARAM *param __attribute__((unused)))
{
  struct ftppc_state *state = (struct ftppc_state*)param->ftparser_state;
  mecab_destroy((mecab_t*)state->engine);
  list_free(state->mem_root, 1);
  
  return(0);
}

static size_t str_convert(CHARSET_INFO *cs, char *from, size_t from_length,
                          CHARSET_INFO *uc, char *to,   size_t to_length){
  char *rpos, *rend, *wpos, *wend;
  my_wc_t wc;
  
  rpos = from;
  rend = from + from_length;
  wpos = to;
  wend = to + to_length;
  while(rpos < rend){
    int cnvres = 0;
    cnvres = cs->cset->mb_wc(cs, &wc, (uchar*)rpos, (uchar*)rend);
    if(cnvres > 0){
      rpos += cnvres;
    }else if(cnvres == MY_CS_ILSEQ){
      rpos++;
      wc = '?';
    }else{
      break;
    }
    cnvres = uc->cset->wc_mb(uc, wc, (uchar*)wpos, (uchar*)wend);
    if(cnvres > 0){
      wpos += cnvres;
    }else{
      break;
    }
  }
  return (size_t)(wpos - to);
}

/**
 * Parse a string and add tokens.
 * @param param MYSQL_FTPARSER_PARAM
 * @param buffer buffer to be parsed by mecab
 * @param buffer_length the length of the buffer
 * @param cs CHARSET_INFO of the table definition.
 * @param need_alloc 
 * @param boolinfo MYSQL_FTPARSER_BOOLEAN_INFO. This may be NULL for natural mode.
 */
static void mecabize_add(MYSQL_FTPARSER_PARAM *param, char *buffer, size_t buffer_length, CHARSET_INFO *cs,
    int need_alloc, MYSQL_FTPARSER_BOOLEAN_INFO *boolinfo){
  struct ftppc_state *state = (struct ftppc_state*)param->ftparser_state;
  mecab_t *mecab = (mecab_t*)state->engine;
  
  char  *feed = buffer;
  size_t feed_length = buffer_length;
  int    feed_req_free = 0;
  CHARSET_INFO *uc= (CHARSET_INFO*)state->engine_charset;
  if(strcmp(cs->csname, uc->csname)!=0){
    feed_length = uc->mbmaxlen * cs->cset->numchars(cs, buffer, buffer+buffer_length);
    feed = (char*)my_malloc(feed_length, MYF(MY_WME));
    feed_length = str_convert(cs, buffer, buffer_length, uc, feed, feed_length);
    feed_req_free = 1;
  }
  
  int save_transcode=0;
  if(strcmp(param->cs->csname, uc->csname)!=0){
    save_transcode=1;
  }
  
  mecab_node_t *node = (mecab_node_t*)mecab_sparse_tonode2(mecab, buffer, buffer_length);
  if(!node){
    fputs(mecab_strerror(mecab), stderr);
    fflush(stderr);
    return; // mecab might not have UTF-8 dictionary in this case.
  }
  
  char*  thead;
  size_t tlen;
  char*  wbuffer = NULL;
  size_t wbuffer_length = 8;
  while(1){
    if(node->stat==MECAB_BOS_NODE || node->stat==MECAB_EOS_NODE){
      // gaps. we know.
    }else{
      if(save_transcode){
        tlen = param->cs->mbmaxlen * uc->cset->numchars(uc, node->surface, node->surface + node->length);
        if(!wbuffer){
          wbuffer = (char*)my_malloc(wbuffer_length, MYF(MY_WME));
        }
        if(wbuffer_length < tlen){
          while(wbuffer_length < tlen){
            wbuffer_length = wbuffer_length << 1;
          }
          char *tmp=(char*)my_realloc(wbuffer, wbuffer_length, MYF(MY_WME));
          if(tmp){ wbuffer = tmp; }
        }
        tlen = str_convert(uc, (char*)node->surface, node->length, cs, wbuffer, wbuffer_length);
        thead = (char*)ftppc_alloc((struct ftppc_state*)param->ftparser_state, tlen);
        memcpy(thead, wbuffer, tlen);
      }else{
        thead = (char*)node->surface;
        tlen = node->length;
        if(need_alloc || feed_req_free){
          thead = (char*)ftppc_alloc((struct ftppc_state*)param->ftparser_state, tlen);
          memcpy(thead, node->surface, tlen);
        }
      }
      if(tlen > 0 && tlen < HA_FT_MAXBYTELEN){
        param->mysql_add_word(param, thead, tlen, boolinfo);
      }else{
        // XXX: too long token. we should raise warn here.
      }
    }
    if(!node->next) break;
    node = node->next;
  }
  if(wbuffer){
    my_free(wbuffer, MYF(0));
  }
  if(feed_req_free){
    my_free(feed, MYF(0));
  }
}


static char* add_token(MYSQL_FTPARSER_PARAM *param, char* feed, size_t feed_length, CHARSET_INFO *cs, MYSQL_FTPARSER_BOOLEAN_INFO *instinfo, int feed_realloc,
    int save_transcode, char* trans, size_t *trans_length_pt){
  int tlen = feed_length;
  char* thead = feed;
  if(tlen <= 0){
    return trans;
   }
  int trans_length = *trans_length_pt;
  if(save_transcode){
    size_t tmp_length = param->cs->mbmaxlen * cs->cset->numchars(cs, feed, feed+feed_length);
    if(trans_length < tmp_length){
      if(trans_length==0){ trans_length=1; }
      while(trans_length < tmp_length){
        trans_length = trans_length<<1;
      }
      if(trans){
        char * tmp = my_realloc(trans, trans_length, MYF(MY_WME));
        if(!tmp){
          // we should raise warn here.
          return trans;
        }
        trans = tmp;
      }else{
        trans = my_malloc(trans_length, MYF(MY_WME));
        if(!trans){
          // we should raise warn here.
          return trans;
        }
      }
    }
    *trans_length_pt = trans_length;
    tlen = str_convert(cs, thead, tlen, param->cs, trans, trans_length);
    thead = trans;
  }
  if(feed_realloc || save_transcode){
    thead = (char*)ftppc_alloc((struct ftppc_state*)param->ftparser_state, tlen);
    if(thead){
      memcpy(thead, feed, tlen);
    }else{
      // we should raise warn here.
      return trans;
    }
  }
  if(tlen < HA_FT_MAXBYTELEN){
    param->mysql_add_word(param, thead, tlen, instinfo);
/*         
//         char buf[1024];
//         memcpy(buf, ftstring_head(pbuffer), tlen);
//         buf[tlen]='\0';
//         fputs(buf, stderr);
//         fputs("\n", stderr);
//         fflush(stderr); */
  }else{
    /* we should raise warn here. */
  }
  return trans;
}


static int mecab_parser_parse(MYSQL_FTPARSER_PARAM *param)
{
  DBUG_ENTER("mecab_parser_parse");

  char* feed = param->doc;
  size_t feed_length = (size_t)param->length;
  int feed_req_free=0;
  CHARSET_INFO *cs = param->cs; // charset of feed
  
#if HAVE_ICU
  // normalize
  if(mecab_unicode_normalize && strcmp(mecab_unicode_normalize, "OFF")!=0){
    if(strcmp(cs->csname, "utf8")!=0){
      // convert into UTF-8
      CHARSET_INFO *uc = get_charset(33,MYF(0)); // we always need to convert to UTF-8 to use mecab.
      // calculate mblen and malloc.
      size_t cv_length = uc->mbmaxlen * cs->cset->numchars(cs, feed, feed+feed_length);
      char* cv = my_malloc(cv_length, MYF(MY_WME));
      feed_length = str_convert(cs, feed, feed_length, uc, cv, cv_length);
      feed = cv;
      feed_req_free = 1;
      cs = uc;
    }
    
    size_t nm_used=0;
    size_t nm_length = feed_length+32;
    char* nm = my_malloc(nm_length, MYF(MY_WME));
    if(!nm){
      if(feed_req_free){ my_free(feed,MYF(0)); }
      DBUG_RETURN(FTPPC_MEMORY_ERROR);
    }
    int mode = UNORM_NONE;
    int options = 0;
    if(strcmp(mecab_unicode_normalize, "C")==0) mode = UNORM_NFC;
    if(strcmp(mecab_unicode_normalize, "D")==0) mode = UNORM_NFD;
    if(strcmp(mecab_unicode_normalize, "KC")==0) mode = UNORM_NFKC;
    if(strcmp(mecab_unicode_normalize, "KD")==0) mode = UNORM_NFKD;
    if(strcmp(mecab_unicode_normalize, "FCD")==0) mode = UNORM_FCD;
    if(mecab_unicode_version && strcmp(mecab_unicode_version, "3.2")==0) options |= UNORM_UNICODE_3_2;
    if(feed_length > 0){
      nm_used = uni_normalize(feed, feed_length, nm, nm_length, mode, options);
      if(nm_used == 0){
        fputs("unicode normalization failed.\n",stderr);
        fflush(stderr);
        
        if(feed_req_free){ my_free(feed,MYF(0)); }
        DBUG_RETURN(FTPPC_NORMALIZATION_ERROR);
      }else if(nm_used > nm_length){
        nm_length = nm_used + 8;
        char *tmp = my_realloc(nm, nm_length, MYF(MY_WME));
        if(tmp){
          nm = tmp;
        }else{
          if(feed_req_free){ my_free(feed,MYF(0)); }
          my_free(nm, MYF(0));
          DBUG_RETURN(FTPPC_MEMORY_ERROR);
        }
        nm_used = uni_normalize(feed, feed_length, nm, nm_length, mode, options);
        if(nm_used == 0){
          fputs("unicode normalization failed.\n",stderr);
          fflush(stderr);
          
          if(feed_req_free){ my_free(feed,MYF(0)); }
          my_free(nm, MYF(0));
          DBUG_RETURN(FTPPC_NORMALIZATION_ERROR);
        }
      }
      if(feed_req_free){ my_free(feed,MYF(0)); }
      feed = nm;
      feed_length = nm_used;
      feed_req_free = 1;
    }
  }
#endif
  
  if(param->mode == MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
    FTSTRING buffer = { NULL, 0, NULL, 0, 0 };
    FTSTRING *pbuffer = &buffer;
    ftstring_bind(pbuffer, feed, feed_req_free);
    
    MYSQL_FTPARSER_BOOLEAN_INFO instinfo ={ FT_TOKEN_WORD, 0, 0, 0, 0, ' ', 0 }; // root boolean_info
    MYSQL_FTPARSER_BOOLEAN_INFO *info_may = (MYSQL_FTPARSER_BOOLEAN_INFO*)my_malloc(sizeof(MYSQL_FTPARSER_BOOLEAN_INFO), MYF(MY_WME));
    *info_may = instinfo;
    LIST *infos = NULL;
    list_push(infos, info_may);
    
    char*  trans = NULL; // transcoding reusable buffer.
    size_t trans_length = 0;
    int save_transcode = 0;
    if(strcmp(param->cs->csname, cs->csname)!=0){
      save_transcode=1;
    }
    
    int context=CTX_CONTROL;
    SEQFLOW sf,sf_prev = SF_BROKEN;
    char* pos = feed;
    char* docend = feed+feed_length;
    while(pos < docend){
      int readsize;
      my_wc_t dst;
      sf = ctxscan(cs, pos, docend, &dst, &readsize, context);
      if(sf==SF_ESCAPE){
        context |= CTX_ESCAPE;
        context |= CTX_CONTROL;
      }else{
        context &= ~CTX_ESCAPE;
        if(sf == SF_CHAR){
          context &= ~CTX_CONTROL;
        }else{
          context |= CTX_CONTROL;
        }
      }
      if(sf != SF_CHAR && sf != SF_ESCAPE){
        if(sf_prev == SF_CHAR){
          if(sf == SF_TRUNC){
            instinfo.trunc = 1;
          }
          size_t tlen = ftstring_length(pbuffer);
          char* thead = ftstring_head(pbuffer);
          if(tlen > 0){
            if(sf == SF_QUOTE_END){ // we need mecab
              mecabize_add(param, thead, tlen, cs, feed_req_free|ftstring_internal(pbuffer), &instinfo);
            }else{ // it is already a token
              trans=add_token(param, thead, tlen, cs, &instinfo, feed_req_free|ftstring_internal(pbuffer), save_transcode, trans, &trans_length);
            }
          }
          ftstring_reset(pbuffer);
          instinfo = *((MYSQL_FTPARSER_BOOLEAN_INFO *)infos->data);
        }
      }
      if(sf == SF_PLUS){   instinfo.yesno = 1; }
      if(sf == SF_MINUS){  instinfo.yesno = -1; }
      if(sf == SF_STRONG){ instinfo.weight_adjust++; }
      if(sf == SF_WEAK){   instinfo.weight_adjust--; }
      if(sf == SF_WASIGN){ instinfo.wasign = !instinfo.wasign; }
      if(sf == SF_LEFT_PAREN){
        MYSQL_FTPARSER_BOOLEAN_INFO *tmp = (MYSQL_FTPARSER_BOOLEAN_INFO*)my_malloc(sizeof(MYSQL_FTPARSER_BOOLEAN_INFO), MYF(MY_WME));
        *tmp = instinfo;
        list_push(infos, tmp);
        
        instinfo.type = FT_TOKEN_LEFT_PAREN;
        param->mysql_add_word(param, pos, 0, &instinfo); // push LEFT_PAREN token
        instinfo = *tmp;
      }
      if(sf == SF_QUOTE_START){
        context |= CTX_QUOTE;
        instinfo.quot=(char*)1;
        
        MYSQL_FTPARSER_BOOLEAN_INFO *tmp = (MYSQL_FTPARSER_BOOLEAN_INFO*)my_malloc(sizeof(MYSQL_FTPARSER_BOOLEAN_INFO), MYF(MY_WME));
        *tmp = instinfo;
        list_push(infos, tmp);
        
        instinfo.type = FT_TOKEN_LEFT_PAREN;
        param->mysql_add_word(param, pos, 0, &instinfo); // push LEFT_PAREN token
        instinfo = *tmp;
      }
      if(sf == SF_RIGHT_PAREN){
        instinfo = *((MYSQL_FTPARSER_BOOLEAN_INFO*)infos->data);
        instinfo.type = FT_TOKEN_RIGHT_PAREN;
        param->mysql_add_word(param, pos, 0, &instinfo); // push RIGHT_PAREN token
        
        MYSQL_FTPARSER_BOOLEAN_INFO *tmp = (MYSQL_FTPARSER_BOOLEAN_INFO*)infos->data;
        if(tmp){ my_free(tmp, MYF(0)); }
        list_pop(infos);
        if(!infos){
          DBUG_RETURN(FTPPC_SYNTAX_ERROR);
        } // must not reach the base info_may level.
        instinfo = *((MYSQL_FTPARSER_BOOLEAN_INFO*)infos->data);
      }
      if(sf == SF_QUOTE_END){
        context &= ~CTX_QUOTE;
        
        instinfo = *((MYSQL_FTPARSER_BOOLEAN_INFO*)infos->data);
        instinfo.type = FT_TOKEN_RIGHT_PAREN;
        instinfo.quot = (char*)1; // This is not required normally.
        param->mysql_add_word(param, pos, 0, &instinfo); // push RIGHT_PAREN token
        
        MYSQL_FTPARSER_BOOLEAN_INFO *tmp = infos->data;
        if(tmp){ my_free(tmp, MYF(0)); }
        list_pop(infos);
        if(!infos){
          DBUG_RETURN(FTPPC_SYNTAX_ERROR);
        } // must not reach the base info_may level.
        instinfo = *((MYSQL_FTPARSER_BOOLEAN_INFO*)infos->data);
      }
      if(sf == SF_CHAR){
        if(ftstring_length(pbuffer)==0){
          ftstring_bind(pbuffer, pos, feed_req_free);
        }
        ftstring_append(pbuffer, pos, readsize);
      }
      
      if(readsize > 0){
        pos += readsize;
      }else if(readsize == MY_CS_ILSEQ){
        pos++;
      }else{
        break;
      }
      sf_prev = sf;
    }
    if(sf==SF_CHAR){
      size_t tlen = ftstring_length(pbuffer);
      char* thead = ftstring_head(pbuffer);
      trans=add_token(param, thead, tlen, cs, &instinfo, feed_req_free|ftstring_internal(pbuffer), save_transcode, trans, &trans_length);
    }
    if(trans){ my_free(trans, MYF(0)); }
    list_free(infos, 1);
    ftstring_destroy(pbuffer);
  }else{
    // Natural query or phrase query.
    mecabize_add(param, feed, feed_length, cs, feed_req_free, NULL);
  }
  DBUG_RETURN(0);
}

static int mecab_file_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
    char buf[255];
    int len=255;
    const char *str;
    
    str = value->val_str(value,buf,&len);
    if(!str) return -1;
    *(const char**)save=str;
    
    if(strlen(str)==0) return -1;
    char* sv;
    char* token=strtok_r((char*)str, ",", &sv);
    while(token != NULL){
      FILE *fp=my_fopen(str, O_RDONLY, MYF(0));
      if(fp==NULL) return -1;
      my_fclose(fp, MYF(0));
      token=strtok_r(NULL, ",", &sv);
    }
    return 0;
}

static int mecab_unicode_version_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
    char buf[8];
    int len=8;
    const char *str;
    
    str = value->val_str(value,buf,&len);
    if(!str) return -1;
    *(const char**)save=str;
    if(len==3){
      if(memcmp(str, "3.2", len)==0) return 0;
    }
    if(len==7){
      if(memcmp(str, "DEFAULT", len)==0) return 0;
    }
    return -1;
}

static int mecab_unicode_normalize_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
    char buf[4];
    int len=4;
    const char *str;
    
    str = value->val_str(value,buf,&len);
    if(!str) return -1;
    *(const char**)save=str;
    if(!get_charset(33,MYF(0))) return -1; // If you don't have utf8 codec in mysql, it fails
    if(len==1){
        if(str[0]=='C') return 0;
        if(str[0]=='D') return 0;
    }
    if(len==2){
        if(str[0]=='K' && str[1]=='C'){ return 0;}
        if(str[0]=='K' && str[1]=='D'){ return 0;}
    }
    if(len==3){
        if(str[0]=='F' && str[1]=='C' && str[2]=='D'){ return 0;}
        if(str[0]=='O' && str[1]=='F' && str[2]=='F'){ return 0;}
    }
    return -1;
}

static struct st_mysql_show_var mecab_status[]=
{
  {"Mecab_info", mecab_info, SHOW_CHAR},
  {0,0,0}
};

static MYSQL_SYSVAR_STR(dicdir, mecab_dicdir,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Mecab system dictionary directory",
  mecab_file_check, NULL, "");

static MYSQL_SYSVAR_STR(userdic, mecab_userdic,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Mecab user dictionary file",
  mecab_file_check, NULL, "");

static MYSQL_SYSVAR_STR(normalization, mecab_unicode_normalize,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Set unicode normalization (OFF, C, D, KC, KD, FCD)",
  mecab_unicode_normalize_check, NULL, "OFF");

static MYSQL_SYSVAR_STR(unicode_version, mecab_unicode_version,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Set unicode version (3.2, DEFAULT)",
  mecab_unicode_version_check, NULL, "DEFAULT");

static struct st_mysql_sys_var* mecab_system_variables[]= {
  MYSQL_SYSVAR(dicdir),
  MYSQL_SYSVAR(userdic),
#if HAVE_ICU
  MYSQL_SYSVAR(normalization),
  MYSQL_SYSVAR(unicode_version),
#endif
  NULL
};


static struct st_mysql_ftparser mecab_parser_descriptor=
{
  MYSQL_FTPARSER_INTERFACE_VERSION, /* interface version      */
  mecab_parser_parse,              /* parsing function       */
  mecab_parser_init,               /* parser init function   */
  mecab_parser_deinit              /* parser deinit function */
};

mysql_declare_plugin(ft_mecab)
{
  MYSQL_FTPARSER_PLUGIN,      /* type                            */
  &mecab_parser_descriptor,  /* descriptor                      */
  "mecab",                   /* name                            */
  "Hiroaki Kawai",            /* author                          */
  "MeCab Full-Text Parser", /* description                     */
  PLUGIN_LICENSE_BSD,
  mecab_parser_plugin_init,  /* init function (when loaded)     */
  mecab_parser_plugin_deinit,/* deinit function (when unloaded) */
  0x0016,                     /* version                         */
  mecab_status,               /* status variables                */
  mecab_system_variables,     /* system variables                */
  NULL
}
mysql_declare_plugin_end;

