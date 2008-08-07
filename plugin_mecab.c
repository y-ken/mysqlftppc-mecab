#include <stdlib.h>
#include <ctype.h>
#include <mysql/my_global.h>
#include <mysql/m_ctype.h>
#include <mysql/my_sys.h>
#include <mecab.h>

#include "ftbool.h"

#include <mysql/plugin.h>

#if !defined(__attribute__) && (defined(__cplusplus) || !defined(__GNUC__)  || __GNUC__ == 2 && __GNUC_MINOR__ < 8)
#define __attribute__(A)
#endif

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


static uint str_convert(CHARSET_INFO *cs, char *from, int from_length,
                        CHARSET_INFO *uc, char *to,   int to_length){
  char *rpos, *rend, *wpos, *wend;
  my_charset_conv_mb_wc mb_wc = cs->cset->mb_wc;
  my_charset_conv_wc_mb wc_mb = uc->cset->wc_mb;
  my_wc_t wc;
  
  rpos = from;
  rend = from + from_length;
  wpos = to;
  wend = to + to_length;
  while(rpos < rend){
    int cnvres = 0;
    cnvres = (*mb_wc)(cs, &wc, (uchar*)rpos, (uchar*)rend);
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
    cnvres = (*wc_mb)(uc, wc, (uchar*)wpos, (uchar*)wend);
    if(cnvres > 0){
      wpos += cnvres;
    }else{
      break;
    }
  }
  return (uint32)(wpos - to);
}

static void mecabize_add(CHARSET_INFO *uc, char *buffer, size_t buffer_len, MYSQL_FTPARSER_PARAM *param, MYSQL_FTPARSER_BOOLEAN_INFO *boolinfo){
  mecab_t *mecab;
  mecab_node_t *node;
  
  uchar* wbuffer;
  size_t wbuffer_len = 0;
  
  int qmode = param->mode;
  mecab = mecab_new(0,NULL);
  node = (mecab_node_t*)mecab_sparse_tonode2(mecab, buffer, buffer_len);
  while(1){
    if(node->stat==MECAB_BOS_NODE || node->stat==MECAB_EOS_NODE){
      // gap of sentence
      if(qmode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
        param->mode = MYSQL_FTPARSER_FULL_BOOLEAN_INFO;
      }
    }else{
      // get binary image for Unicode collation
      int binlen = uc->coll->strnxfrmlen(uc, uc->cset->numchars(uc, node->surface, node->surface + node->length));
      if(wbuffer_len < binlen){
        if(wbuffer_len == 0){
          wbuffer = (uchar*)my_malloc(binlen, MYF(0));
        }else{
          wbuffer = (uchar*)my_realloc(wbuffer,binlen,MYF(0));
        }
        wbuffer_len = binlen;
      }
      binlen = uc->coll->strnxfrm(uc, wbuffer, binlen, node->surface, node->length);
      int c,mark;
      uint t_res= uc->sort_order_big[0][0x20 * uc->sort_order[0]];
      for(mark=0,c=0; c<binlen; c+=2){
        if((*(w_buffer+c) == (t_res>>8)) && (*(w_buffer+c+1) == (t_res&0xFF))){
          // it is space or padding.
        }else{
          mark = c;
        }
      }
      binlen = mark+2;
      
      if(qmode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
        param->mysql_add_word(param, wbuffer, binlen, boolinfo);
        param->mode = MYSQL_FTPARSER_WITH_STOPWORDS;
      }else{
        param->mysql_add_word(param, wbuffer, binlen, NULL);
      }
    }
    if(!node->next) break;
    node = node->next;
  }
  mecab_destroy(mecab);
  if(wbuffer_len > 0) my_no_flags_free(wbuffer);
}

static int mecab_parser_parse(MYSQL_FTPARSER_PARAM *param)
{
  CHARSET_INFO *cs = param->cs;
  CHARSET_INFO *uc = get_charset(192,MYF(0)); // my_charset_utf8_unicode_ci for unicode collation
  size_t (*numchars)(struct charset_info_st*, const char *b, const char *e);
  
  int    buffer_alloc = 0;
  char*  buffer;
  size_t buffer_len;
  
  if(cs->ctype == uc->ctype){
    // it is utf8. no need to convert.
    buffer = param->doc;
    buffer_len = param->length;
  }else{
    // calculate mblen and malloc.
    uint mblen = cs->cset->numchars(cs, param->doc, param->doc+param->length);
    buffer_len = uc->mbmaxlen * mblen;
    buffer = (char*)my_malloc(buffer_len, MYF(0));
    buffer_alloc = 1;
    // convert into utf8
    buffer_len = str_convert(cs, param->doc, param->length, uc, buffer, buffer_len);
  }
  
  // buffer is to be free-ed
  param->flags = MYSQL_FTFLAGS_NEED_COPY;
  
  if(param->mode == MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
    MYSQL_FTPARSER_BOOLEAN_INFO bool_info_may ={ FT_TOKEN_WORD, 0, 0, 0, 0, ' ', 0 };
    MYSQL_FTPARSER_BOOLEAN_INFO instinfo;
    int depth=0;
    MYSQL_FTPARSER_BOOLEAN_INFO baseinfos[16];
    instinfo = baseinfos[0] = bool_info_may;
    
    size_t tlen=0;
    uchar *tmpbuffer;
    tmpbuffer = (uchar*)my_malloc(buffer_len, MYF(0));
    
    int context=CTX_CONTROL;
    SEQFLOW sf,sf_prev = SF_BROKEN;
    char *pos=buffer;
    while(pos < buffer+buffer_len){
      int readsize;
      my_wc_t dst;
      sf = ctxscan(uc, pos, buffer+buffer_len, &dst, &readsize, context);
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
          param->mysql_add_word(param, buffer, 0, &instinfo);
        }
        if(sf == SF_RIGHT_PAREN){
          instinfo.type = FT_TOKEN_RIGHT_PAREN;
          param->mysql_add_word(param, buffer, 0, &instinfo);
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
          mecabize_add(uc, tmpbuffer, tlen, param, &instinfo); // emit
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
      mecabize_add(uc, tmpbuffer, tlen, param, &instinfo);
    }
    
    my_no_flags_free(tmpbuffer);
  }else{
    mecabize_add(uc, buffer, buffer_len, param, NULL);
  }
  
  if(buffer_alloc) my_no_flags_free(buffer);
  return(0);
}


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
  NULL,                       /* system variables                */
  NULL
}
mysql_declare_plugin_end;

