#include <stdlib.h>
#include <ctype.h>
#include <mysql/my_global.h>
#include <mysql/m_ctype.h>
#include <mysql/my_sys.h>
#include <mecab.h>

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

static int mecab_parser_parse(MYSQL_FTPARSER_PARAM *param)
{
  CHARSET_INFO *cs = param->cs;
  CHARSET_INFO *uc = get_charset(192,MYF(0)); // my_charset_utf8_unicode_ci for unicode collation
  size_t (*numchars)(struct charset_info_st*, const char *b, const char *e);
  
  int qmode;
  mecab_t *mecab;
  mecab_node_t *node;
  
  uint mblen;
  
  uchar* wbuffer;
  size_t wbuffer_len;
  
  int    buffer_alloc = 0;
  char*  buffer;
  size_t buffer_len;
  
  if(cs->ctype == uc->ctype){
    // it is utf8. no need to convert.
    buffer = param->doc;
    buffer_len = param->length;
  }else{
    // calculate mblen and malloc.
    numchars = cs->cset->numchars;
    mblen = (*numchars)(cs, param->doc, param->doc+param->length);
    buffer_len = uc->mbmaxlen * mblen;
    buffer = (char*)my_malloc(buffer_len, MYF(0));
    buffer_alloc = 1;
    // convert into utf8
    buffer_len = str_convert(cs, param->doc, param->length, uc, buffer, buffer_len);
  }
  
  // buffer is to be free-ed
  param->flags = MYSQL_FTFLAGS_NEED_COPY;
  MYSQL_FTPARSER_BOOLEAN_INFO bool_info_must ={ FT_TOKEN_WORD, 1, 0, 0, 0, ' ', 0 };
  qmode = param->mode;
  
  numchars = cs->cset->numchars;
  wbuffer_len = 0;
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
      int binlen = uc->coll->strnxfrmlen(uc, (*numchars)(uc, node->surface, node->surface + node->length));
      if(wbuffer_len < binlen){
        if(wbuffer_len == 0){
          wbuffer = (uchar*)my_malloc(binlen, MYF(0));
        }else{
          wbuffer = (uchar*)my_realloc(wbuffer,binlen,MYF(0));
        }
        wbuffer_len = binlen;
      }
      binlen = uc->coll->strnxfrm(uc, wbuffer, binlen, node->surface, node->length);
      
      if(qmode==MYSQL_FTPARSER_FULL_BOOLEAN_INFO){
        param->mysql_add_word(param, wbuffer, binlen, &bool_info_must);
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
  0x0001,                     /* version                         */
  NULL,                       /* status variables                */
  NULL,                       /* system variables                */
  NULL
}
mysql_declare_plugin_end;

