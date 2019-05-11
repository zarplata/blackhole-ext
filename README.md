# blackhole-ext

Blackhole is PHP extension for measuring requests duration and sending it to the StatsD collector.
With supporting tags in the Datadog format.

## Installation

```sh
phpize
./configure
make
```

## Enabling extension

You'll need to add `extension=blackhole.so` to your `php.ini` file.

## Runtime configuration

You must set the hostname of your StatsD collector:

```php
// you may choose the metric name for some cases:
if (PHP_SAPI == 'cli') {
    blackhole_metric_add('cli', 'statsd.telegraf.service.consul', 8125);
} else {
    blackhole_metric_add('http', 'statsd.telegraf.service.consul', 8125);
}
```

You might use tags (when StatsD collector is supporting its):

```php
blackhole_metric_add('http_with_tags', 'statsd.telegraf.service.consul', 8125);
blackhole_metric_set_tag('http_with_tags', 'controller', $controllerName);
blackhole_metric_set_tag('http_with_tags', 'action', $actionName);
```

## Additional functions

```php
>>> blackhole_get_request_started_at()
=> 1536668715.3135
>>> blackhole_get_request_duration()
=> 114.862835
```
