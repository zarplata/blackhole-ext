/*
 * blackhole-ext
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <netinet/in.h>
#include <netdb.h>

#include "php.h"
#include "ext/standard/info.h"

#include "php_blackhole.h"

ZEND_DECLARE_MODULE_GLOBALS(blackhole)

#ifdef COMPILE_DL_BLACKHOLE
ZEND_GET_MODULE(blackhole)
#endif

#define MICRO_IN_SEC 1000000.00

/* {{{ internal funcs */

static inline double php_blackhole_timeval_to_double(struct timeval tp)
{
    return tp.tv_sec + tp.tv_usec / MICRO_IN_SEC;
}

static blackhole_statsd *php_blackhole_statsd_open(const char *host, int port) /* {{{ */
{
    if (!host || !port) {
        return NULL;
    }

    blackhole_statsd *statsd = malloc(sizeof(blackhole_statsd));
    if (statsd == NULL) {
        php_error_docref(NULL, E_WARNING, "unable to allocate memory for statsd");
        return NULL;
    }

    if ((statsd->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == FAILURE) {
        php_error_docref(NULL, E_WARNING, "failed to initialize StatsD UDP socket");
        free(statsd);
        return NULL;
    }

    memset(&statsd->server, 0, sizeof(statsd->server));
    statsd->server.sin_family = AF_INET;
    statsd->server.sin_port = htons(port);

    struct addrinfo *result = NULL, hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    int error;
    if ((error = getaddrinfo(host, NULL, &hints, &result))) {
        php_error_docref(NULL, E_WARNING, "failed to resolve StatsD server hostname '%s': %s", host, gai_strerror(error));
        free(statsd);
        return NULL;
    }

    memcpy(&(statsd->server.sin_addr), &((struct sockaddr_in*)result->ai_addr)->sin_addr, sizeof(struct in_addr));
    freeaddrinfo(result);

    return statsd;
}
/* }}} */

static int php_blackhole_statsd_send(blackhole_statsd *statsd, char *data)
{
    ssize_t sent;

    sent = sendto(statsd->sock, data, strlen(data), 0, (struct sockaddr *)&statsd->server, sizeof(statsd->server));

    return sent != FAILURE ? SUCCESS : FAILURE;
}

static void php_blackhole_statsd_close(blackhole_statsd *statsd) /* {{{ */
{
    if (statsd == NULL) {
        return;
    }
    if (statsd->sock != FAILURE) {
        close(statsd->sock);
        statsd->sock = FAILURE;
    }
    free(statsd);
}
/* }}} */

static double php_blackhole_request_duration() /* {{{ */
{
    struct timeval tp;
    if (gettimeofday(&tp, NULL) == FAILURE) {
        return 0;
    }
    timersub(&tp, &BLACKHOLE_G(request_started_at), &tp);
    return php_blackhole_timeval_to_double(tp);
}
/* }}} */

static int php_blackhole_str_append(char **str, const char *buf) /* {{{ */
{
    if (*str == NULL) {
        size_t size = strlen(*str);
        str = malloc(size + 1);
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
static char *php_blackhole_create_request_data(const char *metric_name, HashTable *tags, int request_duration_ms)
{
    if (metric_name == NULL) {
        return NULL;
    }

    char *data;
    int tags_cnt = tags == NULL ? 0 : zend_hash_num_elements(tags);

    if (tags_cnt == 0) {
        if (asprintf(&data, "%s:%d|ms", metric_name, request_duration_ms) < SUCCESS) {
            php_error_docref(NULL, E_WARNING, "failed to allocate request data");
            return NULL;
        }
        return data;
    }

    if (asprintf(&data, "%s:%d|ms|#", metric_name, request_duration_ms) < SUCCESS) {
        php_error_docref(NULL, E_WARNING, "failed to allocate request data");
        return NULL;
    }

    HashPosition position;
    zval *zv;
    int i = 0;

    for (zend_hash_internal_pointer_reset_ex(tags, &position);
         (zv = zend_hash_get_current_data_ex(tags, &position)) != NULL;
         zend_hash_move_forward_ex(tags, &position)) {
        zend_string *key;
        zend_ulong index;
        char *tag_value = Z_PTR_P(zv);
        i++;

        if (zend_hash_get_current_key_ex(tags, &key, &index, &position) == HASH_KEY_IS_STRING) {
            char *tag_buf;
            if (i < tags_cnt) {
                if (asprintf(&tag_buf, "%s:%s,", key->val, tag_value) < SUCCESS) {
                    php_error_docref(NULL, E_WARNING, "failed to allocate tag string for request data");
                    return NULL;
                }
            } else {
                if (asprintf(&tag_buf, "%s:%s", key->val, tag_value) < SUCCESS) {
                    php_error_docref(NULL, E_WARNING, "failed to allocate tag string for request data");
                    return NULL;
                }
            }
            php_blackhole_str_append(&data, tag_buf);
            free(tag_buf);
        } else {
            continue;
        }
    }

    return data;
}
/* }}} */

static blackhole_metric *php_blackhole_find_metric(const char *name) /* {{{ */
{
    for (int i = 0; i < BLACKHOLE_G(metrics_initialized); i++) {
        blackhole_metric *metric = &BLACKHOLE_G(metrics)[i];
        if (strcmp(metric->name, name) == 0) {
            return metric;
        }
    }
    return NULL;
}
/* }}} */

static int php_blackhole_send_metric(blackhole_metric *metric, int request_duration_ms) /* {{{ */
{
    char *data;
    blackhole_statsd *statsd;
    ssize_t sent;

    data = php_blackhole_create_request_data(metric->name, &metric->tags, request_duration_ms);
    if (data == NULL) {
        return FAILURE;
    }

    statsd = php_blackhole_statsd_open(metric->host, metric->port);
    if (statsd == NULL) {
        return FAILURE;
    }

    int isSent = SUCCESS;

    sent = php_blackhole_statsd_send(statsd, data);
    if (sent == FAILURE) {
        php_error_docref(NULL, E_WARNING, "failed to send metric to StatsD: %s", strerror(errno));
        isSent = FAILURE;
    }

    php_blackhole_statsd_close(statsd);
    free(data);

    return isSent;
}
/* }}} */

static void php_blackhole_free_metric(blackhole_metric *metric) /* {{{ */
{
    zend_hash_destroy(&metric->tags);

    if (metric->host) {
        efree(metric->host);
        metric->host = NULL;
    }

    if (metric->name) {
        efree(metric->name);
        metric->name = NULL;
    }
}
/* }}} */

static void php_blackhole_tag_dtor(zval *zv) /* {{{ */
{
    char *tag = Z_PTR_P(zv);
    if (tag) {
        efree(tag);
    }
}

/* {{{ proto string blackhole_get_host()
   Get StatsD host */
static PHP_FUNCTION(blackhole_get_host)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_EMPTY_STRING();
}

/* {{{ proto bool blackhole_set_host(string host)
   Set StatsD host */
static PHP_FUNCTION(blackhole_set_host)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto string blackhole_get_port()
   Set StatsD port */
static PHP_FUNCTION(blackhole_get_port)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_LONG(0)
}
/* }}} */

/* {{{ proto bool blackhole_set_port(int port)
   Set StatsD port */
static PHP_FUNCTION(blackhole_set_port)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto string blackhole_get_metric_name()
   Set StatsD metric name */
static PHP_FUNCTION(blackhole_get_metric_name)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_EMPTY_STRING()
}

/* {{{ proto bool blackhole_set_metric_name(string metric_name)
   Set StatsD metric name */
static PHP_FUNCTION(blackhole_set_metric_name)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_TRUE;
}

/* {{{ proto string blackhole_get_overall_metric_name()
   Set StatsD metric name */
static PHP_FUNCTION(blackhole_get_overall_metric_name)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_EMPTY_STRING()
}

/* {{{ proto bool blackhole_set_overall_metric_name(string metric_name)
   Set StatsD metric name */
static PHP_FUNCTION(blackhole_set_overall_metric_name)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_TRUE;
}

/* {{{ proto array blackhole_get_tags()
   Get request tags */
static PHP_FUNCTION(blackhole_get_tags)
{
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "") != SUCCESS) {
        return;
    }
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    array_init(return_value);
}

/* {{{ proto bool blackhole_set_tag(string tag, string value)
   Set request tag */
static PHP_FUNCTION(blackhole_set_tag)
{
    php_error_docref(NULL, E_DEPRECATED, "function is deprecated");
    RETURN_TRUE;
}
/* }}} */

// NEW API
static PHP_FUNCTION(blackhole_metric_add) /* {{{ */
{
    char *name, *host;
    size_t name_len, host_len;
    long port = 8125;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss|l",
            &name, &name_len, &host, &host_len, &port) != SUCCESS) {
        return;
    }

    unsigned int n = BLACKHOLE_G(metrics_initialized);
    if (n == BLACKHOLE_METRICS_MAX) {
        php_error_docref(NULL, E_WARNING, "metric %s not added: limit exceeded", name);
        return;
    }
    BLACKHOLE_G(metrics_initialized)++;

    blackhole_metric *metric = &BLACKHOLE_G(metrics)[n];

    if (metric->name) {
        efree(metric->name);
    }
    if (metric->host) {
        efree(metric->host);
    }

    metric->name = estrndup(name, name_len);
    metric->host = estrndup(host, host_len);
    metric->port = port;

    zend_hash_init(&metric->tags, 10, NULL, php_blackhole_tag_dtor, 0);

    RETURN_TRUE;
}

static PHP_FUNCTION(blackhole_metric_set_tag) /* {{{ */
{
    char *name, *tag, *value;
    size_t name_len, tag_len, value_len;

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "sss",
            &name, &name_len, &tag, &tag_len, &value, &value_len) != SUCCESS) {
        return;
    }

    blackhole_metric *metric = php_blackhole_find_metric(name);

    if (metric == NULL) {
        php_error_docref(NULL, E_WARNING, "metric not found");
        RETURN_FALSE;
    }

    if (tag_len < 1) {
        php_error_docref(NULL, E_WARNING, "tag name cannot be empty");
        RETURN_FALSE;
    }

    if (strstr(tag, ":") != NULL) {
        php_error_docref(NULL, E_WARNING, "tag name is not allowed to contain colons");
        RETURN_FALSE;
    }

    if (strstr(value, ":") != NULL) {
        php_error_docref(NULL, E_WARNING, "tag value is not allowed to contain colons");
        RETURN_FALSE;
    }

    zend_hash_str_update_ptr(&metric->tags, tag, tag_len, estrndup(value, value_len));
    RETURN_TRUE;
}
/* }}} */

/* {{{ proto array blackhole_get_request_duration()
   Get request duration */
static PHP_FUNCTION(blackhole_get_request_duration)
{
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "") != SUCCESS) {
        return;
    }

    RETURN_DOUBLE(php_blackhole_request_duration());
}
/* }}} */

/* {{{ proto array blackhole_get_request_started_at()
   Get request start time */
static PHP_FUNCTION(blackhole_get_request_started_at)
{
    if (zend_parse_parameters(ZEND_NUM_ARGS(), "") != SUCCESS) {
        return;
    }

    RETURN_DOUBLE(php_blackhole_timeval_to_double(BLACKHOLE_G(request_started_at)));
}
/* }}} */

/* {{{ proto string blackhole_get_data()
    */
static PHP_FUNCTION(blackhole_get_data)
{
    int request_duration_ms = (int) (php_blackhole_request_duration() * 1000);

    if (zend_parse_parameters(ZEND_NUM_ARGS(), "") != SUCCESS) {
        return;
    }

    array_init(return_value);
    for (int i = 0; i < BLACKHOLE_G(metrics_initialized); i++) {
        blackhole_metric metric = BLACKHOLE_G(metrics)[i];
        char *data = php_blackhole_create_request_data(metric.name, &metric.tags, request_duration_ms);
        char *key;
        asprintf(&key, "%d#%s#%d", i, metric.host, metric.port);
        add_assoc_string_ex(return_value, key, strlen(key), data);
    }
}
/* }}} */

/* {{{ arginfo */
// new API
ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_metric_add, 0, 0, 3)
    ZEND_ARG_INFO(0, metric_name)
    ZEND_ARG_INFO(0, host)
    ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_metric_set_tag, 0, 0, 3)
    ZEND_ARG_INFO(0, metric_name)
    ZEND_ARG_INFO(0, tag)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

// legacy API (stubs for BC)
ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_host, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_host, 0, 0, 1)
    ZEND_ARG_INFO(0, host)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_port, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_port, 0, 0, 1)
    ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_metric_name, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_metric_name, 0, 0, 1)
    ZEND_ARG_INFO(0, metric_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_overall_metric_name, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_overall_metric_name, 0, 0, 1)
    ZEND_ARG_INFO(0, metric_name)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_tags, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_set_tag, 0, 0, 2)
    ZEND_ARG_INFO(0, tag)
    ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

// internal API
ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_request_duration, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_request_started_at, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_blackhole_get_data, 0, 0, 0)
ZEND_END_ARG_INFO()
/* }}} */

#define BLACKHOLE_FUNC(func) PHP_FE(func, arginfo_ ## func)

/* {{{ blackhole_functions[]
 */
zend_function_entry blackhole_functions[] = {
    // legacy metrics API
    BLACKHOLE_FUNC(blackhole_get_host)
    BLACKHOLE_FUNC(blackhole_set_host)
    BLACKHOLE_FUNC(blackhole_get_port)
    BLACKHOLE_FUNC(blackhole_set_port)
    BLACKHOLE_FUNC(blackhole_get_metric_name)
    BLACKHOLE_FUNC(blackhole_set_metric_name)
    BLACKHOLE_FUNC(blackhole_get_overall_metric_name)
    BLACKHOLE_FUNC(blackhole_set_overall_metric_name)
    BLACKHOLE_FUNC(blackhole_get_tags)
    BLACKHOLE_FUNC(blackhole_set_tag)

    // new metrics API
    BLACKHOLE_FUNC(blackhole_metric_add)
    BLACKHOLE_FUNC(blackhole_metric_set_tag)

    BLACKHOLE_FUNC(blackhole_get_request_duration)
    BLACKHOLE_FUNC(blackhole_get_request_started_at)
    BLACKHOLE_FUNC(blackhole_get_data)
    {NULL, NULL, NULL}
};
/* }}} */

/* {{{ php_blackhole_init_globals
 */
static void php_blackhole_init_globals(zend_blackhole_globals *globals)
{
    memset(globals, 0, sizeof(*globals));

    globals->metrics_initialized = 0;
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

/* {{{ PHP_RINIT_FUNCTION
 */
static PHP_RINIT_FUNCTION(blackhole)
{
    struct timeval t;

    if (gettimeofday(&t, NULL) == SUCCESS) {
        (&BLACKHOLE_G(request_started_at))->tv_sec = t.tv_sec;
        (&BLACKHOLE_G(request_started_at))->tv_usec = t.tv_usec;
    } else {
        return FAILURE;
    }

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
static PHP_RSHUTDOWN_FUNCTION(blackhole)
{
    int request_duration_ms = (int) (php_blackhole_request_duration() * 1000);

    for (int i = 0; i < BLACKHOLE_G(metrics_initialized); i++) {
        php_blackhole_send_metric(&BLACKHOLE_G(metrics)[i], request_duration_ms);
        php_blackhole_free_metric(&BLACKHOLE_G(metrics)[i]);
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
    NULL,
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
