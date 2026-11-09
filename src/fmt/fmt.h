#ifndef MOXY_FMT_H
#define MOXY_FMT_H

#include "conf/conf.h"

/* reformat Moxy source; line breaks are kept, spacing and indentation normalized */
char *fmt_source(const char *src, const MoxyConfig *cfg);

#endif
