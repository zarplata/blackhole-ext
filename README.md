# blackhole-ext

Blackhole is PHP extension for sending request duration to the StatsD collector.
Supporting tags in Datadog format.

## Installation

```sh
phpize
./configure
make
```

## Enabling extension

You'll need to add `extension=blackhole.so` to your `php.ini` file.

## Usage

Metrics will not be sent if `host` or `metric_name` is not set.

```php
<?php

blackhole_set_host('statsd.telegraf.service.consul');

// you may choose the metric name for your cases:
if (PHP_SAPI == 'cli') {
    blackhole_set_metric_name('http_requests');
} else {
    blackhole_set_metric_name('cli');
}
```
 
Also you can change default `8125` port:

```php
<?php

blackhole_set_port(1234);
```

If your StatsD collector might receive tags in Datadog format:

```php
<?php

blackhole_set_tag('controller', $controllerName);
blackhole_set_tag('action', $actionName);
```

For some cases you can fetch info: 

```
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
