/*
 * blackhole-ext
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netinet/in.h>
#include <netdb.h>

#include "php.h"
#include "SAPI.h"
#include "ext/standard/info.h"

#include "php_blackhole.h"

ZEND_DECLARE_MODULE_GLOBALS(blackhole)

#ifdef COMPILE_DL_BLACKHOLE
ZEND_GET_MODULE(blackhole)
#endif

#define MICRO_IN_SEC 1000000.00
#define timeval_cvt(a, b) do { (a)->tv_sec = (b)->tv_sec; (a)->tv_usec = (b)->tv_usec; } while (0);
#define timeval_to_double(tp) (double)(tp.tv_sec + tp.tv_usec / MICRO_IN_SEC)

/* {{{ internal funcs */

static int php_blackhole_statsd_init(blackhole_statsd *statsd, const char *host, int port) /* {{{ */
{
    if (!host || !port) {
        return FAILURE;
    }

    if ((statsd->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == FAILURE) {
        php_error_docref(NULL, E_WARNING, "failed init StatsD socket");
        return FAILURE;
    }

    memset(&statsd->server, 0, sizeof(statsd->server));
    statsd->server.sin_family = AF_INET;
    statsd->server.sin_port = htons(port);

    struct addrinfo *result = NULL, hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int error;
    if ( (error = getaddrinfo(host, NULL, &hints, &result)) ) {
        php_error_docref(NULL, E_WARNING, "failed to resolve StatsD server hostname '%s': %s", host, gai_strerror(error));
        return FAILURE;
    }

    memcpy(&(statsd->server.sin_addr), &((struct sockaddr_in*)result->ai_addr)->sin_addr, sizeof(struct in_addr));
    freeaddrinfo(result);

    return SUCCESS;
}
/* }}} */

static inline void php_blackhole_statsd_close(blackhole_statsd *statsd) /* {{{ */
{
    if (statsd == NULL) {
        return;
    }
    if (statsd->sock != FAILURE) {
        close(statsd->sock);
        statsd->sock = FAILURE;
    }
}
/* }}} */

static double php_blackhole_request_duration() /* {{{ */
{
    struct timeval tp;
    if (gettimeofday(&tp, NULL) == FAILURE) {
        return 0;
    }
    timersub(&tp, &BLACKHOLE_G(request_started_at), &tp);
    return timeval_to_double(tp);
}
/* }}} */

static inline int php_blackhole_str_append(char **str, const char *buf, int size) /* {{{ */
{
    if (*str == NULL) {
        str = malloc((size_t)size + 1);
        memcpy(str, buf, size);
        str[size] = NULL;
        return SUCCESS;
    }

    char *str_new;
    if (asprintf(&str_new, "%s%s", *str, buf) == FAILURE) {
        return FAILURE;
    }
    free(*str);
    *str = str_new;
    return SUCCESS;
}
/* }}} */

/* {{{ test_metric:456|g|#test_tag1:test_value1,test_tag2:test_value2
 */
static char *php_blackhole_create_request_data()
{
    if (BLACKHOLE_G(metric_name) == NULL) {
        return NULL;
    }

    char *data;
    HashTable *tags = &BLACKHOLE_G(tags);
    int tags_cnt = zend_hash_num_elements(tags);
    int request_duration_ms = (int) (php_blackhole_request_duration() * 1000);

    if (tags_cnt == 0) {
        asprintf(&data, "%s:%d|ms", BLACKHOLE_G(metric_name), request_duration_ms);
        return data;
    }

    asprintf(&data, "%s:%d|ms|#", BLACKHOLE_G(metric_name), request_duration_ms);

    HashPosition position;
    zval *zv;
    int i = 0;

    for (zend_hash_internal_pointer_reset_ex(&BLACKHOLE_G(tags), &position);
         (zv = zend_hash_get_current_data_ex(&BLACKHOLE_G(tags), &position)) != NULL;
         zend_hash_move_forward_ex(&BLACKHOLE_G(tags), &position)) {
        zend_string *key;
        zend_ulong index;
        char *tag_value = Z_PTR_P(zv);
        i++;

        if (zend_hash_get_current_key_ex(&BLACKHOLE_G(tags), &key, &index, &position) == HASH_KEY_IS_STRING) {
            char *tag_buf;
            if (i < tags_cnt) {
                asprintf(&tag_buf, "%s:%s,", key->val, tag_value);
            } else {
                asprintf(&tag_buf, "%s:%s", key->val, tag_value);
            }
            php_blackhole_str_append(&data, tag_buf, (int) strlen(tag_buf));
            free(tag_buf);
        } else {
            continue;
        }
    }

    return data;
}
/* }}} */

static inline int php_blackhole_send_data(blackhole_statsd *statsd) /* {{{ */
{
    char *data;
    ssize_t sent;

    if (statsd->sock == FAILURE) {
        return FAILURE;
    }

    data = php_blackhole_create_request_data();
    if (data == NULL) {
        return FAILURE;
    }

    sent = sendto(statsd->sock, data, strlen(data), 0, (struct sockaddr *)&statsd->server, sizeof(statsd->server));
    if (sent == FAILURE) {
        php_error_docref(NULL, E_WARNING, "failed to send metrics to StatsD: %s", strerror(errno));
    }

    free(data);

    return sent == FAILURE ? FAILURE : SUCCESS;
}
/* }}} */

static void php_blackhole_tag_dtor(zval *zv) /* {{{ */
{
    char *tag = Z_PTR_P(zv);
    if (tag) {
        efree(tag);
    }
}
/* }}} */

/* {{{ proto array blackhole_get_info()
   Get request info */
static PHP_FUNCTION(blackhole_get_info)
{
    zval tags;
    HashPosition pos;
    zval *zv;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "") != SUCCESS) {
        return;
    }

    array_init(return_value);

    if (BLACKHOLE_G(host) != NULL) {
        add_assoc_string(return_value, "host", BLACKHOLE_G(host));
    } else {
        add_assoc_string(return_value, "host", "");
    }
    add_assoc_long(return_value, "port", BLACKHOLE_G(port));
    add_assoc_double(return_value, "request_duration", php_blackhole_request_duration());
    add_assoc_double(return_value, "request_started_at", timeval_to_double(BLACKHOLE_G(request_started_at)));

    array_init(&tags);

    for (zend_hash_internal_pointer_reset_ex(&BLACKHOLE_G(tags), &pos);
         (zv = zend_hash_get_current_data_ex(&BLACKHOLE_G(tags), &pos)) != NULL;
         zend_hash_move_forward_ex(&BLACKHOLE_G(tags), &pos)) {
        zend_string *tag_key;
        zend_ulong tag_index;
        char *tag_value = Z_PTR_P(zv);

        if (zend_hash_get_current_key_ex(&BLACKHOLE_G(tags), &tag_key, &tag_index, &pos) == HASH_KEY_IS_STRING) {
            add_assoc_string_ex(&tags, tag_key->val, tag_key->len, tag_value);
        } else {
            continue;
        }
    }
    add_assoc_zval(return_value, "tags", &tags);
}
/* }}} */

/* {{{ proto string blackhole_get_data()
    */
static PHP_FUNCTION(blackhole_get_data)
{
    char *data;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "") != SUCCESS) {
        return;
    }

    data = php_blackhole_create_request_data();
    if (!data) {
        RETURN_NULL();
    }
    RETVAL_STRING(data);
    free(data);
}
/* }}} */

/* {{{ proto bool blackhole_set_tag(string tag, string value)
   Set request tag */
static PHP_FUNCTION(blackhole_set_tag)
{
    char *name, *value;
    size_t name_len, value_len;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss", &name, &name_len, &value, &value_len) != SUCCESS) {
        return;
    }

    if (name_len < 1) {
        php_error_docref(NULL, E_WARNING, "tag name cannot be empty");
        RETURN_FALSE;
    }

    value = estrndup(value, value_len);

    zend_hash_str_update_ptr(&BLACKHOLE_G(tags), name, name_len, value);
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool blackhole_set_host(string host)
   Set custom server name */
static PHP_FUNCTION(blackhole_set_host)
{
    char *host;
    size_t host_len;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &host, &host_len) != SUCCESS) {
        return;
    }

    if (BLACKHOLE_G(host)) {
        efree(BLACKHOLE_G(host));
    }

    BLACKHOLE_G(host) = estrndup(host, host_len);
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool blackhole_set_port(int port)
   Set custom server name */
static PHP_FUNCTION(blackhole_set_port)
{
    int port;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &port) != SUCCESS) {
        return;
    }

    BLACKHOLE_G(port) = port;
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool blackhole_set_metric_name(string metric_name)
   Set custom server name */
static PHP_FUNCTION(blackhole_set_metric_name)
{
    char *metric_name;
    size_t metric_name_len;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &metric_name, &metric_name_len) != SUCCESS) {
        return;
    }

    if (BLACKHOLE_G(metric_name)) {
        efree(BLACKHOLE_G(metric_name));
    }

    BLACKHOLE_G(metric_name) = estrndup(metric_name, metric_name_len);
    RETURN_TRUE;
}
/* }}} */

/* {{{ arginfo */

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_info, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_data, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_host, 0, 0, 1)
    ZEND_ARG_INFO(0, host)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_port, 0, 0, 1)
    ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_metric_name, 0, 0, 1)
    ZEND_ARG_INFO(0, metric_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_tag, 0, 0, 2)
    ZEND_ARG_INFO(0, tag)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

/* }}} */

#define BLACKHOLE_FUNC(func) PHP_FE(func, arginfo_ ## func)

/* {{{ blackhole_functions[]
 */
zend_function_entry blackhole_functions[] = {
    BLACKHOLE_FUNC(blackhole_get_info)
    BLACKHOLE_FUNC(blackhole_get_data)
    BLACKHOLE_FUNC(blackhole_set_host)
    BLACKHOLE_FUNC(blackhole_set_port)
    BLACKHOLE_FUNC(blackhole_set_metric_name)
    BLACKHOLE_FUNC(blackhole_set_tag)
    {NULL, NULL, NULL}
};
/* }}} */

/* {{{ php_blackhole_init_globals
 */
static void php_blackhole_init_globals(zend_blackhole_globals *globals)
{
    memset(globals, 0, sizeof(*globals));

    globals->metric_name = NULL;
    globals->host = NULL;
    globals->port = BLACKHOLE_STATSD_DEFAULT_PORT;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
static PHP_MINIT_FUNCTION(blackhole)
{
    ZEND_INIT_MODULE_GLOBALS(blackhole, php_blackhole_init_globals, NULL);
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
static PHP_MSHUTDOWN_FUNCTION(blackhole)
{
    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION
 */
static PHP_RINIT_FUNCTION(blackhole)
{
    struct timeval t;

    if (gettimeofday(&t, NULL) == SUCCESS) {
        timeval_cvt(&BLACKHOLE_G(request_started_at), &t);
    } else {
        return FAILURE;
    }

    zend_hash_init(&BLACKHOLE_G(tags), 10, NULL, php_blackhole_tag_dtor, 0);

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
static PHP_RSHUTDOWN_FUNCTION(blackhole)
{
    if (php_blackhole_statsd_init(&BLACKHOLE_G(statsd), BLACKHOLE_G(host), BLACKHOLE_G(port)) == SUCCESS) {
        php_blackhole_send_data(&BLACKHOLE_G(statsd));
    }
    php_blackhole_statsd_close(&BLACKHOLE_G(statsd));

    zend_hash_destroy(&BLACKHOLE_G(tags));

    if (BLACKHOLE_G(host)) {
        efree(BLACKHOLE_G(host));
        BLACKHOLE_G(host) = NULL;
    }

    if (BLACKHOLE_G(metric_name)) {
        efree(BLACKHOLE_G(metric_name));
        BLACKHOLE_G(metric_name) = NULL;
    }

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
static PHP_MINFO_FUNCTION(blackhole)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "Blackhole support", "enabled");
    php_info_print_table_row(2, "Blackhole extension version", PHP_BLACKHOLE_VERSION);
    php_info_print_table_end();
}
/* }}} */

/* {{{ blackhole_module_entry
 */
zend_module_entry blackhole_module_entry = {
    STANDARD_MODULE_HEADER,
    "blackhole",
    blackhole_functions,
    PHP_MINIT(blackhole),
    PHP_MSHUTDOWN(blackhole),
    PHP_RINIT(blackhole),
    PHP_RSHUTDOWN(blackhole),
    PHP_MINFO(blackhole),
    PHP_BLACKHOLE_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: sw=4 ts=4 fdm=marker
 * vim<600: sw=4 ts=4
 */
