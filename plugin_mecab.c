#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <mecab.h>

#include "ftbool.h"
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
#include <plugin.h>
#define HA_FT_MAXBYTELEN 254

#if !defined(__attribute__) && (defined(__cplusplus) || !defined(__GNUC__)  || __GNUC__ == 2 && __GNUC_MINOR__ < 8)
#define __attribute__(A)
#endif

static char* mecab_unicode_normalize;
static char* mecab_unicode_version;
static char  mecab_info[128];
static char* mecab_dicdir;
static char* mecab_userdic;
static my_bool mecab_partial;

static void* icu_malloc(const void* context, size_t size){ return my_malloc(size,MYF(MY_WME)); }
static void* icu_realloc(const void* context, void* ptr, size_t size){ return my_realloc(ptr,size,MYF(MY_WME)); }
static void  icu_free(const void* context, void *ptr){ my_free(ptr,MYF(0)); }

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
  return(0);
}
static int mecab_parser_deinit(MYSQL_FTPARSER_PARAM *param __attribute__((unused)))
{
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
 * @param buffer buffer to be parsed by mecab
 * @param buffer_len the length of the buffer
 * @param param MYSQL_FTPARSER_PARAM
 * @param boolinfo MYSQL_FTPARSER_BOOLEAN_INFO. This may be NULL for natural mode.
 * @param cs CHARSET_INFO of the table definition.
 * @param isToken 0 if the buffer was phrase. 1 if the buffer was a token itself.
 */
static void mecabize_add(char *buffer, size_t buffer_len,
    MYSQL_FTPARSER_PARAM *param, MYSQL_FTPARSER_BOOLEAN_INFO *boolinfo, CHARSET_INFO *cs, int isToken){
  mecab_t *mecab;
  mecab_node_t *node;
  CHARSET_INFO *uc=NULL;
  if(strcmp(cs->csname,"utf8")!=0){
    uc=get_charset(33,MYF(0));
  }
  
  int inquot=0;
  size_t wbuffer_len = 128;
  uchar* wbuffer = (uchar*)my_malloc(wbuffer_len,MYF(MY_WME));
  
  if(isToken){
    if(uc){
      int binlen = cs->mbmaxlen * uc->cset->numchars(uc, buffer, buffer+buffer_len);
      if(wbuffer_len < binlen){
        wbuffer = (uchar*)my_realloc(wbuffer,binlen,MYF(MY_WME));
        wbuffer_len = binlen;
      }
      binlen = str_convert(uc, (char*)buffer, buffer_len, cs, (char*)wbuffer, binlen);
      if(binlen>0 && binlen < HA_FT_MAXBYTELEN){
        param->mysql_add_word(param, (char*)wbuffer, binlen, boolinfo);
      }
    }else{
      if(buffer_len>0 && buffer_len < HA_FT_MAXBYTELEN){
        param->mysql_add_word(param, (char*)buffer, buffer_len, boolinfo);
      }
    }
    return;
  }
  
  int qmode = param->mode;
  char *arg="";
  if(strlen(mecab_dicdir)>0){
    strcat(arg, " -d ");
    strcat(arg, mecab_dicdir);
  }
  if(strlen(mecab_userdic)>0){
    strcat(arg, " -u ");
    strcat(arg, mecab_userdic);
  }
  if(mecab_partial==TRUE){
    strcat(arg, " -p ");
  }
  mecab = mecab_new2(arg);
  node = (mecab_node_t*)mecab_sparse_tonode2(mecab, buffer, buffer_len);
  if(!node) return; // mecab might not have UTF-8 dictionary in this case.
  
  while(1){
    if(node->stat==MECAB_BOS_NODE){
      // emission is delayed until a token is found.
    }else if(node->stat==MECAB_EOS_NODE){
      if(param->mode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO && boolinfo->quot){
        boolinfo->type = FT_TOKEN_RIGHT_PAREN;
        param->mysql_add_word(param, buffer, 0, boolinfo);
        boolinfo->type = FT_TOKEN_WORD;
        boolinfo->quot = NULL;
      }
    }else{
      if(param->mode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO && boolinfo->quot && !inquot){
        boolinfo->quot = (char*)1;
        boolinfo->type = FT_TOKEN_LEFT_PAREN;
        param->mysql_add_word(param, buffer, 0, boolinfo);
        boolinfo->type = FT_TOKEN_WORD;
        inquot=1;
      }
      if(uc){
        int binlen = cs->mbmaxlen * uc->cset->numchars(uc, node->surface, node->surface + node->length);
        if(wbuffer_len < binlen){
          wbuffer = (uchar*)my_realloc(wbuffer,binlen,MYF(MY_WME));
          wbuffer_len = binlen;
        }
        binlen = str_convert(uc, (char*)node->surface, node->length, cs, (char*)wbuffer, binlen);
        if(binlen>0 && binlen < HA_FT_MAXBYTELEN){
          param->mysql_add_word(param, (char*)wbuffer, binlen, boolinfo);
        }
      }else{
        if(node->length>0 && node->length < HA_FT_MAXBYTELEN){
          param->mysql_add_word(param, (char*)node->surface, node->length, boolinfo);
        }
      }
    }
    if(!node->next) break;
    node = node->next;
  }
  mecab_destroy(mecab);
  if(param->mode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO && boolinfo->quot){
    boolinfo->type = FT_TOKEN_RIGHT_PAREN;
    param->mysql_add_word(param, buffer, 0, boolinfo);
    boolinfo->type = FT_TOKEN_WORD;
    boolinfo->quot = NULL;
  }
  my_free(wbuffer,MYF(0));
}

static int mecab_parser_parse(MYSQL_FTPARSER_PARAM *param)
{
  DBUG_ENTER("mecab_parser_parse");

  CHARSET_INFO *uc = NULL;
  CHARSET_INFO *cs = param->cs;
  char* feed = param->doc;
  size_t feed_length = (size_t)param->length;
  int feed_req_free=0;
  
  if(strcmp(cs->csname, "utf8")!=0){
    uc = get_charset(33,MYF(0)); // we always need to convert to UTF-8 to use mecab.
  }
  // convert into UTF-8
  if(uc){
    // calculate mblen and malloc.
    size_t cv_length = uc->mbmaxlen * cs->cset->numchars(cs, feed, feed+feed_length);
    char* cv = my_malloc(cv_length, MYF(MY_WME));
    feed_length = str_convert(cs, feed, feed_length, uc, cv, cv_length);
    feed = cv;
    feed_req_free = 1;
  }
  
#if HAVE_ICU
  // normalize
  if(strcmp(mecab_unicode_normalize, "OFF")!=0){
    char* nm;
    char* t;
    size_t nm_length = feed_length+32;
    size_t nm_used=0;
    nm = my_malloc(nm_length, MYF(MY_WME));
    int status = 0;
    int mode = UNORM_NONE;
    int options = 0;
    if(strcmp(mecab_unicode_normalize, "C")==0) mode = UNORM_NFC;
    if(strcmp(mecab_unicode_normalize, "D")==0) mode = UNORM_NFD;
    if(strcmp(mecab_unicode_normalize, "KC")==0) mode = UNORM_NFKC;
    if(strcmp(mecab_unicode_normalize, "KD")==0) mode = UNORM_NFKD;
    if(strcmp(mecab_unicode_normalize, "FCD")==0) mode = UNORM_FCD;
    if(strcmp(mecab_unicode_version, "3.2")==0) options |= UNORM_UNICODE_3_2;
    t = uni_normalize(feed, feed_length, nm, nm_length, &nm_used, mode, &status);
    if(status != 0){
      nm_length=nm_used;
      nm = my_realloc(nm, nm_length, MYF(MY_WME));
      t = uni_normalize(feed, feed_length, nm, nm_length, &nm_used, mode, &status);
      if(status != 0){
        fputs("unicode normalization failed.\n",stderr);
        fflush(stderr);
      }else{
        nm = t;
      }
    }else{
      nm = t;
    }
    feed_length = nm_used;
    if(feed_req_free) my_free(feed,MYF(0));
    feed = nm;
    feed_req_free = 1;
  }
#endif
  
  // buffer is to be free-ed
  param->flags |= MYSQL_FTFLAGS_NEED_COPY;
  
  if(param->mode == MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
    MYSQL_FTPARSER_BOOLEAN_INFO bool_info_may ={ FT_TOKEN_WORD, 0, 0, 0, 0, ' ', 0 };
    MYSQL_FTPARSER_BOOLEAN_INFO instinfo;
    int depth=0;
    MYSQL_FTPARSER_BOOLEAN_INFO baseinfos[16];
    instinfo = baseinfos[0] = bool_info_may;
    
    size_t tlen=0;
    char* tmpbuffer;
    tmpbuffer = my_malloc(feed_length, MYF(0));
    
    int context=CTX_CONTROL;
    SEQFLOW sf,sf_prev = SF_BROKEN;
    char* pos = feed;
    char* docend = feed+feed_length;
    while(pos < docend){
      int readsize;
      my_wc_t dst;
      if(uc){
        sf = ctxscan(uc, pos, docend, &dst, &readsize, context);
      }else{
        sf = ctxscan(cs, pos, docend, &dst, &readsize, context);
      }
      
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
        if(sf == SF_QUOTE_START){
          context |= CTX_QUOTE;
          instinfo.quot=(char*)1;
        }
        if(sf == SF_QUOTE_END){
          context &= ~CTX_QUOTE;
        }
        if(sf == SF_LEFT_PAREN){
          instinfo = baseinfos[depth];
          depth++;
          if(depth>16) depth=16;
          baseinfos[depth] = instinfo;
          instinfo.type = FT_TOKEN_LEFT_PAREN;
          if(param->mode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
            param->mysql_add_word(param, feed, 0, &instinfo); // push LEFT_PAREN token
          }
        }
        if(sf == SF_RIGHT_PAREN){
          instinfo.type = FT_TOKEN_RIGHT_PAREN;
          if(param->mode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
            param->mysql_add_word(param, feed, 0, &instinfo); // push RIGHT_PAREN token
          }
          depth--;
          if(depth<0) depth=0;
        }
        if(sf == SF_PLUS){
          instinfo.yesno = 1;
        }
        if(sf == SF_MINUS){
          instinfo.yesno = -1;
        }
        if(sf == SF_PLUS) instinfo.weight_adjust = 1;
        if(sf == SF_MINUS) instinfo.weight_adjust = -1;
        if(sf == SF_WASIGN){
          instinfo.wasign = -1;
        }
        if(sf == SF_WHITE || sf == SF_QUOTE_END || sf == SF_LEFT_PAREN || sf == SF_RIGHT_PAREN || sf == SF_TRUNC){
          if(sf_prev == SF_CHAR){
            if(sf == SF_TRUNC){
              instinfo.trunc = 1;
            }
            if(sf==SF_QUOTE_END){
              mecabize_add(tmpbuffer, tlen, param, &instinfo, cs, 0); // emit
            }else{
              mecabize_add(tmpbuffer, tlen, param, &instinfo, cs, 1); // emit
            }
          }
          instinfo = baseinfos[depth];
        }
        if(sf == SF_CHAR){
          memcpy(tmpbuffer+tlen, pos, readsize);
          tlen += readsize;
        }else if(sf != SF_ESCAPE){
          tlen = 0;
        }
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
      mecabize_add(tmpbuffer, tlen, param, &instinfo, cs, 1);
    }
    
    my_free(tmpbuffer, MYF(0));
  }else{
    mecabize_add(feed, feed_length, param, NULL, cs, 0);
  }
  if(feed_req_free) my_free(feed, MYF(0));
  
  DBUG_RETURN(0);
}

int mecab_file_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
    char buf[4];
    int len=4;
    const char *str;
    
    str = value->val_str(value,buf,&len);
    if(!str) return -1;
    *(const char**)save=str;
    
    if(strlen(str)==0) return -1;
    char* tokens=strtok((char*)str, ",");
    while(tokens != NULL){
      FILE *fp=fopen(str, "r");
      if(fp==NULL) return -1;
      fclose(fp);
    }
    return 0;
}

int mecab_unicode_version_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
    char buf[4];
    int len=4;
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

int mecab_unicode_normalize_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
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
  {"Mecab_info", (char *)mecab_info, SHOW_CHAR},
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

static MYSQL_SYSVAR_BOOL(partial, mecab_partial, 
  PLUGIN_VAR_OPCMDARG,
  "Mecab partial mode",
  NULL, NULL, FALSE);

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
  MYSQL_SYSVAR(partial),
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
  0x0014,                     /* version                         */
  mecab_status,               /* status variables                */
  mecab_system_variables,     /* system variables                */
  NULL
}
mysql_declare_plugin_end;

