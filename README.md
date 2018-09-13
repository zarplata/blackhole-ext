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
blackhole_set_host('statsd.telegraf.service.consul');
```

Also, you can change default `8125` port:

```php
blackhole_set_port(1234);
```

Set the metric name:

```php
// you may choose the metric name for some cases:
if (PHP_SAPI == 'cli') {
    blackhole_set_metric_name('cli');
} else {
    blackhole_set_metric_name('http_requests');
}
```

You might use tags (when StatsD collector is supporting its):

```php
blackhole_set_tag('controller', $controllerName);
blackhole_set_tag('action', $actionName);
```

When use tags you might need to have the overall metric:

```php
blackhole_set_overall_metric_name('http_requests_overall');
```

## Additional functions

```php
>>> blackhole_get_host()
=> "statsd.telegraf.service.consul"
>>> blackhole_get_port()
=> 8125
>>> blackhole_get_metric_name()
=> "http_requests"
>>> blackhole_get_tags()
=> [
     "controller" => "MainController",
     "action" => "indexAction",
   ]
>>> blackhole_get_request_started_at()
=> 1536668715.3135
>>> blackhole_get_request_duration()
=> 114.862835
```
