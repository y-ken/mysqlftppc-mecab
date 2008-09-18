#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <mecab.h>

#include "ftbool.h"
#include "ftnorm.h"

// mysql headers
#include <my_global.h>
#include <m_ctype.h>
#include <my_sys.h>
#include <plugin.h>

#if !defined(__attribute__) && (defined(__cplusplus) || !defined(__GNUC__)  || __GNUC__ == 2 && __GNUC_MINOR__ < 8)
#define __attribute__(A)
#endif

static char* mecab_unicode_normalize;
static char* mecab_collation;

static int mecab_parser_plugin_init(void *arg __attribute__((unused)))
{
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


static size_t str_convert(CHARSET_INFO *cs, char *from, int from_length,
                          CHARSET_INFO *uc, char *to,   int to_length){
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
    }else if(cnvres > MY_CS_TOOSMALL){
      rpos += (-cnvres);
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

static void mecabize_add(char *buffer, size_t buffer_len,
    MYSQL_FTPARSER_PARAM *param, MYSQL_FTPARSER_BOOLEAN_INFO *boolinfo, CHARSET_INFO *collator, CHARSET_INFO *cs){
  mecab_t *mecab;
  mecab_node_t *node;
  CHARSET_INFO *uc = get_charset(33,MYF(0));
  
  uchar* wbuffer;
  size_t wbuffer_len = 0;
  
  int qmode = param->mode;
  mecab = mecab_new(0,NULL);
  if(buffer[buffer_len]=='\0') buffer_len -= 1;
  node = (mecab_node_t*)mecab_sparse_tonode2(mecab, buffer, buffer_len);
  if(!node) return; // mecab might not have UTF-8 dictionary in this case.
  
  while(1){
    if(node->stat==MECAB_BOS_NODE || node->stat==MECAB_EOS_NODE){
      // gap of sentence
      if(qmode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
        param->mode = MYSQL_FTPARSER_FULL_BOOLEAN_INFO;
      }
    }else{
      int binlen;
      if(collator){
        // get binary image for Unicode collation
        binlen = collator->coll->strnxfrmlen(collator, uc->cset->numchars(uc, node->surface, node->surface + node->length));
        if(wbuffer_len < binlen){
          if(wbuffer_len == 0){
            wbuffer = (uchar*)my_malloc(binlen, MYF(MY_WME));
          }else{
            wbuffer = (uchar*)my_realloc(wbuffer,binlen,MYF(MY_WME));
          }
          wbuffer_len = binlen;
        }
        binlen = collator->coll->strnxfrm(collator, wbuffer, binlen, (uchar*)node->surface, (size_t)node->length);
      }else{
        binlen = cs->mbmaxlen * uc->cset->numchars(uc, node->surface, node->surface + node->length);
        if(wbuffer_len < binlen){
          if(wbuffer_len == 0){
            wbuffer = (uchar*)my_malloc(binlen, MYF(MY_WME));
          }else{
            wbuffer = (uchar*)my_realloc(wbuffer,binlen,MYF(MY_WME));
          }
          wbuffer_len = binlen;
        }
        binlen = str_convert(uc, (char*)node->surface, node->length, cs, (char*)wbuffer, binlen);
      }
      
      if(qmode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
        param->mysql_add_word(param, (char*)wbuffer, binlen, boolinfo);
        param->mode = MYSQL_FTPARSER_WITH_STOPWORDS;
      }else{
        param->mysql_add_word(param, (char*)wbuffer, binlen, NULL);
      }
    }
    if(!node->next) break;
    node = node->next;
  }
  mecab_destroy(mecab);
  if(wbuffer_len > 0) my_free(wbuffer,MYF(0));
}

static int mecab_parser_parse(MYSQL_FTPARSER_PARAM *param)
{
  CHARSET_INFO *uc = NULL;
  CHARSET_INFO *collator = NULL; // if we use collation, it is not null
  CHARSET_INFO *cs = param->cs;
  char* feed = param->doc;
  size_t feed_length = (size_t)param->length;
  
  size_t mblen;
  char* cv;
  size_t cv_length=0;
  
  uc = get_charset(33,MYF(0)); // we always need to convert to UTF-8 to use mecab.
  
  // convert into UTF-8
  if(uc){
    // calculate mblen and malloc.
    mblen = uc->mbmaxlen * cs->cset->numchars(cs, feed, feed+feed_length);
    cv = my_malloc(mblen, MYF(MY_WME));
    cv_length = mblen;
    feed_length = str_convert(cs, feed, feed_length, uc, cv, cv_length);
    feed = cv;
  }
  
#if HAVE_ICU
  // normalize
  if(strcmp(mecab_unicode_normalize, "OFF")!=0){
    char* nm;
    size_t nm_length=0;
    size_t nm_used=0;
    nm_length = feed_length+32;
    nm = my_malloc(nm_length, MYF(MY_WME));
    int status = 0;
    int mode = 1;
    if(strcmp(mecab_unicode_normalize, "C")==0) mode = 4;
    if(strcmp(mecab_unicode_normalize, "D")==0) mode = 2;
    if(strcmp(mecab_unicode_normalize, "KC")==0) mode = 5;
    if(strcmp(mecab_unicode_normalize, "KD")==0) mode = 3;
    if(strcmp(mecab_unicode_normalize, "FCD")==0) mode = 6;
    nm = uni_normalize(feed, feed_length, nm, nm_length, &nm_used, mode, &status);
    if(status < 0){
       nm_length=nm_used;
       nm = my_realloc(nm, nm_length, MYF(MY_WME));
       nm = uni_normalize(feed, feed_length, nm, nm_length, &nm_used, mode, &status);
    }
    if(cv_length){
      cv = my_realloc(cv, nm_used, MYF(MY_WME));
    }else{
      cv = my_malloc(nm_used, MYF(MY_WME));
    }
    memcpy(cv, nm, nm_used);
    cv_length = nm_used;
    my_free(nm,MYF(0));
    feed = cv;
    feed_length = cv_length;
  }
#endif
  
  if(strcmp(mecab_collation, "OFF")!=0){
//     bin 90
//     czech 138
//     roman 143
//     danish 139
//     slovak 141
//     polish 133
//     spanish 135
//     swedish 136
//     turkish 137
//     general 35
//     unicode 128
//     latvian 130
//     persian 144
//     spanish2 142
//     romanian 131
//     estonian 134
//     icelandic 129
//     slovenian 132
//     esperanto 145
//     hungarian 146
//     lithuanian 140
    // charset is always UCS2
    if(strcmp(mecab_collation, "bin")==0) collator = get_charset(90,MYF(0));
    if(strcmp(mecab_collation, "czech")==0) collator = get_charset(138,MYF(0));
    if(strcmp(mecab_collation, "roman")==0) collator = get_charset(143,MYF(0));
    if(strcmp(mecab_collation, "danish")==0) collator = get_charset(139,MYF(0));
    if(strcmp(mecab_collation, "slovak")==0) collator = get_charset(141,MYF(0));
    if(strcmp(mecab_collation, "polish")==0) collator = get_charset(133,MYF(0));
    if(strcmp(mecab_collation, "spanish")==0) collator = get_charset(135,MYF(0));
    if(strcmp(mecab_collation, "swedish")==0) collator = get_charset(136,MYF(0));
    if(strcmp(mecab_collation, "turkish")==0) collator = get_charset(137,MYF(0));
    if(strcmp(mecab_collation, "general")==0) collator = get_charset(35,MYF(0));
    if(strcmp(mecab_collation, "unicode")==0) collator = get_charset(128,MYF(0));
    if(strcmp(mecab_collation, "latvian")==0) collator = get_charset(130,MYF(0));
    if(strcmp(mecab_collation, "persian")==0) collator = get_charset(144,MYF(0));
    if(strcmp(mecab_collation, "spanish2")==0) collator = get_charset(142,MYF(0));
    if(strcmp(mecab_collation, "romanian")==0) collator = get_charset(131,MYF(0));
    if(strcmp(mecab_collation, "estonian")==0) collator = get_charset(134,MYF(0));
    if(strcmp(mecab_collation, "icelandic")==0) collator = get_charset(129,MYF(0));
    if(strcmp(mecab_collation, "slovenian")==0) collator = get_charset(132,MYF(0));
    if(strcmp(mecab_collation, "esperanto")==0) collator = get_charset(145,MYF(0));
    if(strcmp(mecab_collation, "hungarian")==0) collator = get_charset(146,MYF(0));
    if(strcmp(mecab_collation, "lithuanian")==0) collator = get_charset(140,MYF(0));
  }
  
  // buffer is to be free-ed
  int qmode = param->mode;
  param->flags = MYSQL_FTFLAGS_NEED_COPY;
  
  if(qmode == MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
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
      if(collator && sf==SF_TRUNC) sf=SF_CHAR;
      
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
        if(sf == SF_QUOTE_START) context |= CTX_QUOTE;
        if(sf == SF_QUOTE_END)   context &= ~CTX_QUOTE;
        if(sf == SF_LEFT_PAREN){
          instinfo = baseinfos[depth];
          depth++;
          if(depth>16) depth=16;
          baseinfos[depth] = instinfo;
          instinfo.type = FT_TOKEN_LEFT_PAREN;
          param->mysql_add_word(param, feed, 0, &instinfo); // push LEFT_PAREN token
        }
        if(sf == SF_RIGHT_PAREN){
          instinfo.type = FT_TOKEN_RIGHT_PAREN;
          param->mysql_add_word(param, feed, 0, &instinfo); // push RIGHT_PAREN token
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
      }
      if(sf == SF_WHITE || sf == SF_QUOTE_END || sf == SF_LEFT_PAREN || sf == SF_RIGHT_PAREN || sf == SF_TRUNC){
        if(sf_prev == SF_CHAR){
          if(sf == SF_TRUNC){
            instinfo.trunc = 1;
          }
          mecabize_add(tmpbuffer, tlen, param, &instinfo, collator, cs); // emit
        }
        instinfo = baseinfos[depth];
      }
      if(sf == SF_CHAR){
        memcpy(tmpbuffer+tlen, pos, readsize);
        tlen += readsize;
      }else if(sf != SF_ESCAPE){
        tlen = 0;
      }
      
      if(readsize > 0){
        pos += readsize;
      }else if(readsize == MY_CS_ILSEQ){
        pos++;
      }else if(readsize > MY_CS_TOOSMALL){
        pos += (-readsize);
      }else{
        break;
      }
      sf_prev = sf;
    }
    if(sf==SF_CHAR){
      mecabize_add(tmpbuffer, tlen, param, &instinfo, collator, cs);
    }
    
    my_free(tmpbuffer, MYF(0));
  }else{
    mecabize_add(feed, feed_length, param, NULL, collator, cs);
  }
  
  if(cv_length) my_free(cv, MYF(0));
  return(0);
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
int mecab_collation_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value){
    char buf[32];
    int len=32;
    const char *str;
    
    str = value->val_str(value,buf,&len);
    if(!str) return -1;
    *(const char**)save=str;
//     bin 90
//     czech 138
//     roman 143
//     danish 139
//     slovak 141
//     polish 133
//     spanish 135
//     swedish 136
//     turkish 137
//     general 35
//     unicode 128
//     latvian 130
//     persian 144
//     spanish2 142
//     romanian 131
//     estonian 134
//     icelandic 129
//     slovenian 132
//     esperanto 145
//     hungarian 146
//     lithuanian 140
    if(len==3){
        if(memcmp("OFF", str, len)==0) return 0;
        if(memcmp("bin", str, len)==0 && get_charset(90, MYF(0))) return 0;
    }
    if(len==5){
        if(memcmp("czech", str, len)==0 && get_charset(138, MYF(0))) return 0;
        if(memcmp("roman", str, len)==0 && get_charset(143, MYF(0))) return 0;
    }
    if(len==6){
        if(memcmp("danish", str, len)==0 && get_charset(139, MYF(0))) return 0;
        if(memcmp("slovak", str, len)==0 && get_charset(141, MYF(0))) return 0;
        if(memcmp("polish", str, len)==0 && get_charset(133, MYF(0))) return 0;
    }
    if(len==7){
        if(memcmp("spanish", str, len)==0 && get_charset(135, MYF(0))) return 0;
        if(memcmp("swedish", str, len)==0 && get_charset(136, MYF(0))) return 0;
        if(memcmp("turkish", str, len)==0 && get_charset(137, MYF(0))) return 0;
        if(memcmp("general", str, len)==0 && get_charset(35, MYF(0))) return 0;
        if(memcmp("unicode", str, len)==0 && get_charset(128, MYF(0))) return 0;
        if(memcmp("latvian", str, len)==0 && get_charset(130, MYF(0))) return 0;
        if(memcmp("persian", str, len)==0 && get_charset(144, MYF(0))) return 0;
    }
    if(len==8){
        if(memcmp("spanish2", str, len)==0 && get_charset(142, MYF(0))) return 0;
        if(memcmp("romanian", str, len)==0 && get_charset(131, MYF(0))) return 0;
        if(memcmp("estonian", str, len)==0 && get_charset(134, MYF(0))) return 0;
    }
    if(len==9){
        if(memcmp("icelandic", str, len)==0 && get_charset(129, MYF(0))) return 0;
        if(memcmp("slovenian", str, len)==0 && get_charset(132, MYF(0))) return 0;
        if(memcmp("esperanto", str, len)==0 && get_charset(145, MYF(0))) return 0;
        if(memcmp("hungarian", str, len)==0 && get_charset(146, MYF(0))) return 0;
    }
    if(len==10){
        if(memcmp("lithuanian", str, len)==0 && get_charset(140, MYF(0))) return 0;
    }
    return -1;
}
static MYSQL_SYSVAR_STR(normalization, mecab_unicode_normalize,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Set unicode normalization (OFF, C, D, KC, KD, FCD)",
  mecab_unicode_normalize_check, NULL, "OFF");

static MYSQL_SYSVAR_STR(collation, mecab_collation,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Set unicode collation mode (OFF, unicode, general, etc.)",
  mecab_collation_check, NULL, "OFF");

static struct st_mysql_sys_var* mecab_system_variables[]= {
  MYSQL_SYSVAR(collation),
#if HAVE_ICU
  MYSQL_SYSVAR(normalization),
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
  0x0010,                     /* version                         */
  NULL,                       /* status variables                */
  mecab_system_variables,     /* system variables                */
  NULL
}
mysql_declare_plugin_end;

