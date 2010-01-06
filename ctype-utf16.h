#ifdef __cplusplus
extern "C" {
#endif

#include "mysqldep.h"

/*
   This codec only implements mb_wc, wc_mb. The other features are missing now.
*/

CHARSET_INFO* get_ft_charset_utf16be_general_ci();
CHARSET_INFO* get_ft_charset_utf16le_general_ci();

#ifdef __cplusplus
}
#endif
