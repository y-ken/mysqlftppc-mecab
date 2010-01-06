#ifdef __cplusplus
extern "C" {
#endif

#include <plugin.h>
static char* mecab_eolstyle;
static char* mecab_normalization;
static char* mecab_unicode_version;
static char  mecab_info[128];
static char* mecab_dicdir;
static char* mecab_userdic;

static int mecab_dicdir_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value);
static int mecab_userdic_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value);
static int mecab_eolstyle_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value);
static int mecab_normalization_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value);
static int mecab_unicode_version_check(MYSQL_THD thd, struct st_mysql_sys_var *var, void *save, struct st_mysql_value *value);

static int mecab_parser_plugin_init(void *arg);
static int mecab_parser_plugin_deinit(void *arg);

static int mecab_parser_parse(MYSQL_FTPARSER_PARAM *param);
static int mecab_parser_init(MYSQL_FTPARSER_PARAM *param);
static int mecab_parser_deinit(MYSQL_FTPARSER_PARAM *param);


static MYSQL_SYSVAR_STR(dicdir, mecab_dicdir,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Mecab system dictionary directory",
  mecab_dicdir_check, NULL, "");

static MYSQL_SYSVAR_STR(userdic, mecab_userdic,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Mecab user dictionary file",
  mecab_userdic_check, NULL, "");

static MYSQL_SYSVAR_STR(eolstyle, mecab_eolstyle,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "End of line style (IGNORE, EOL, DOUBLE)",
  mecab_eolstyle_check, NULL, "DOUBLE");

static MYSQL_SYSVAR_STR(normalization, mecab_normalization,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Set unicode normalization (OFF, C, D, KC, KD, FCD)",
  mecab_normalization_check, NULL, "OFF");

static MYSQL_SYSVAR_STR(unicode_version, mecab_unicode_version,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
  "Set ICU unicode version (3.2, DEFAULT)",
  mecab_unicode_version_check, NULL, "DEFAULT");

static struct st_mysql_sys_var* mecab_system_variables[]= {
  MYSQL_SYSVAR(dicdir),
  MYSQL_SYSVAR(userdic),
  MYSQL_SYSVAR(eolstyle),
#if HAVE_ICU
  MYSQL_SYSVAR(normalization),
  MYSQL_SYSVAR(unicode_version),
#endif
  NULL
};


static struct st_mysql_show_var mecab_status[]= {
  {"Mecab_info", mecab_info, SHOW_CHAR},
  {NULL,NULL,SHOW_UNDEF}
};


static struct st_mysql_ftparser mecab_parser_descriptor= {
	MYSQL_FTPARSER_INTERFACE_VERSION, /* interface version      */
	mecab_parser_parse,              /* parsing function       */
	mecab_parser_init,               /* parser init function   */
	mecab_parser_deinit              /* parser deinit function */
};

mysql_declare_plugin(ft_mecab){
	MYSQL_FTPARSER_PLUGIN,      /* type                            */
	&mecab_parser_descriptor,  /* descriptor                      */
	"mecab",                   /* name                            */
	"Hiroaki Kawai",            /* author                          */
	"MeCab Full-Text Parser", /* description                     */
	PLUGIN_LICENSE_BSD,
	mecab_parser_plugin_init,  /* init function (when loaded)     */
	mecab_parser_plugin_deinit,/* deinit function (when unloaded) */
	0x0200,                     /* version                         */
	mecab_status,               /* status variables                */
	mecab_system_variables,     /* system variables                */
	NULL
}
mysql_declare_plugin_end;


#ifdef __cplusplus
}

#include <mecab.h>
#include "mempool.h"
#include "reader_norm.h"

class FtMecabState {
public:
	FtMemPool *pool;
	MeCab::Tagger *engine;
	CHARSET_INFO *engine_charset;
	enum FtLineStyle eolstyle;
	enum FtNormalization normalization;
	bool unicode_v32;
	FtMecabState();
	~FtMecabState();
};

#endif
