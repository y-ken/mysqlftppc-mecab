#include "ctype-utf16.h"

/*
 * Note: MySQL supports old ucs2.
*/

static int ft_utf16be_uni(CHARSET_INFO *cs __attribute__((unused)),
	my_wc_t *pwc, const uchar *s, const uchar *e){
	if(0xD800<=s[0] && s[0]<=0xDBFF){
		if(s+4>e) return MY_CS_TOOSMALL4;
		if(0xDC00<=s[2] && s[2]<=0xDFFF){
			*pwc = ((s[0]&0x03)<<18)+(s[1]<<10)+((s[2]&0x03)<<8)+s[3];
			return 4;
		}else{
			return MY_CS_ILSEQ;
		}
	}else{
		if(s+2>e) return MY_CS_TOOSMALL2;
		*pwc = (s[0]<<8) + s[1];
		return 2;
	}
}

static int ft_uni_utf16be(CHARSET_INFO *cs, my_wc_t wc, uchar *r, uchar *e){
	if(wc <= 0xFFFF){
		if(r+2>e) return MY_CS_TOOSMALL2;
		r[0] = (uchar)(wc & 0xFF);
		r[1] = (uchar)(wc >> 8);
		return 2;
	}else if(wc <= 0x10FFFF){ // surrogate pair
		if(r+4>e) return MY_CS_TOOSMALL4;
		wc = wc - 0x10000;
		r[0] = 0x11011000 + (wc>>18);
		r[1] = (wc>>10)&0xFF;
		r[2] = 0x11011100 + (wc>>8);
		r[3] = wc&0xFF;
		return 4;
	}else{
		return MY_CS_ILSEQ;
	}
}

static int ft_utf16le_uni(CHARSET_INFO *cs __attribute__((unused)),
	my_wc_t *pwc, const uchar *s, const uchar *e){
	if(s+2>e) return MY_CS_TOOSMALL2;
	
	if(0xD800<=s[1] && s[1]<=0xDBFF){
		if(s+4>e) return MY_CS_TOOSMALL4;
		if(0xDC00<=s[3] && s[3]<=0xDFFF){
			*pwc = ((s[1]&0x03)<<18)+(s[0]<<10)+((s[3]&0x03)<<8)+s[2];
			return 4;
		}else{
			return MY_CS_ILSEQ;
		}
	}else{
		*pwc = (s[0]<<8) + s[1];
		return 2;
	}
}

static int ft_uni_utf16le(CHARSET_INFO *cs, my_wc_t wc, uchar *r, uchar *e){
	if(wc <= 0xFFFF){
		if(r+2>e) return MY_CS_TOOSMALL2;
		r[1] = (uchar)(wc & 0xFF);
		r[0] = (uchar)(wc >> 8);
		return 2;
	}else if(wc <= 0x10FFFF){ // surrogate pair
		if(r+4>e) return MY_CS_TOOSMALL4;
		wc = wc - 0x10000;
		r[1] = 0x11011000 + (wc>>18);
		r[0] = (wc>>10)&0xFF;
		r[3] = 0x11011100 + (wc>>8);
		r[2] = wc&0xFF;
		return 4;
	}else{
		return MY_CS_ILSEQ;
	}
}

MY_CHARSET_HANDLER ft_charset_utf16be_handler = {
	NULL, /* init */
	NULL, /* ismbchar */
	NULL, /* mbcharlen */
	NULL, /* numchars */
	NULL, /* charpos */
	NULL, /* well_formed_len */
	NULL, /* lengthsp */
	NULL, /* numcells */
	ft_utf16be_uni, /* mb_wc */
	ft_uni_utf16be, /* wc_mb */
	NULL, /* ctype */
	NULL, /* caseup_str */
	NULL, /* casedn_str */
	NULL, /* caseup */
	NULL, /* casedn */
	NULL, /* snprintf */
	NULL, /* long10_to_str */
	NULL, /* longlong10_to_str */
	NULL, /* fill */
	NULL, /* strntol */
	NULL, /* strntoul */
	NULL, /* strntoll */
	NULL, /* strntod */
	NULL, /* strtoll10 */
	NULL, /* strntoull10rnd */
	NULL /* scan */
};
CHARSET_INFO ft_charset_utf16be_general_ci = {
	0, 0, 0,
	MY_CS_UNICODE,
	"utf16be",
	"utf16be_dummy_ci",
	"",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	1,
	1,
	1,
	2,
	4,
	0,
	0xFFFF, // XXX: limited to UCS2
	' ',
	0,
	&ft_charset_utf16be_handler,
	NULL
};

MY_CHARSET_HANDLER ft_charset_utf16le_handler = {
	NULL, /* init */
	NULL, /* ismbchar */
	NULL, /* mbcharlen */
	NULL, /* numchars */
	NULL, /* charpos */
	NULL, /* well_formed_len */
	NULL, /* lengthsp */
	NULL, /* numcells */
	ft_utf16le_uni, /* mb_wc */
	ft_uni_utf16le, /* wc_mb */
	NULL, /* ctype */
	NULL, /* caseup_str */
	NULL, /* casedn_str */
	NULL, /* caseup */
	NULL, /* casedn */
	NULL, /* snprintf */
	NULL, /* long10_to_str */
	NULL, /* longlong10_to_str */
	NULL, /* fill */
	NULL, /* strntol */
	NULL, /* strntoul */
	NULL, /* strntoll */
	NULL, /* strntod */
	NULL, /* strtoll10 */
	NULL, /* strntoull10rnd */
	NULL /* scan */
};
CHARSET_INFO ft_charset_utf16le_general_ci = {
	0, 0, 0,
	MY_CS_UNICODE,
	"utf16le",
	"utf16le_dummy_ci",
	"",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	1,
	1,
	1,
	2,
	4,
	0,
	0xFFFF, // XXX: limited to UCS2
	' ',
	0,
	&ft_charset_utf16le_handler,
	NULL
};

CHARSET_INFO* get_ft_charset_utf16be_general_ci(){
	return &ft_charset_utf16be_general_ci;
}

CHARSET_INFO* get_ft_charset_utf16le_general_ci(){
	return &ft_charset_utf16le_general_ci;
}

