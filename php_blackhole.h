/*
 * blackhole-ext
 */


#ifndef PHP_BLACKHOLE_H
#define PHP_BLACKHOLE_H

extern zend_module_entry blackhole_module_entry;
#define phpext_blackhole_ptr &blackhole_module_entry

#ifdef ZTS
#include "TSRM.h"
#endif

#define PHP_BLACKHOLE_VERSION "1.0.0"
#define BLACKHOLE_STATSD_DEFAULT_PORT 8125

typedef struct {
    struct sockaddr_in server;
    int sock;
} blackhole_statsd;

ZEND_BEGIN_MODULE_GLOBALS(blackhole) /* {{{ */
    blackhole_statsd statsd;
    char *host;
    int port;
    char *metric_name;
    double request_time;
    struct timeval request_started_at;
    HashTable tags;
ZEND_END_MODULE_GLOBALS(blackhole)
/* }}} */

#ifdef ZTS
#define BLACKHOLE_G(v) TSRMG(blackhole_globals_id, zend_blackhole_globals *, v)
#else
#define BLACKHOLE_G(v) (blackhole_globals.v)
#endif

#endif	/* PHP_BLACKHOLE_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */