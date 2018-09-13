# project
cmake_minimum_required(VERSION 3.8)
project(blackhole C)

# make
add_custom_target(run
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND make &&
        php -r '
            #blackhole_set_tag\(\"controller_action\", \"main::index\"\)\;

            blackhole_set_tag\(\"controller\", \"main\"\)\;
            blackhole_set_tag\(\"action\", \"index\"\)\;
            blackhole_set_host\(\"127.0.0.1\"\)\;
            blackhole_set_metric_name\(\"this_is_my_metric\"\)\;
            blackhole_set_overall_metric_name\(\"this_is_my_overall_metric\"\)\;

            echo \"request_data: \", blackhole_get_data\(\), PHP_EOL\;
            echo \"request_overall_data: \", blackhole_get_overall_data\(\), PHP_EOL\;
            echo \"host: \", blackhole_get_host\(\), PHP_EOL\;
            echo \"port: \", blackhole_get_port\(\), PHP_EOL\;
            echo \"metric_name: \", blackhole_get_metric_name\(\), PHP_EOL\;
            echo \"overall_metric_name: \", blackhole_get_overall_metric_name\(\), PHP_EOL\;
            echo \"request_started_at: \", blackhole_get_request_started_at\(\), PHP_EOL\;
            echo \"request_duration: \", blackhole_get_request_duration\(\), PHP_EOL\;
            var_dump\(blackhole_get_tags\(\)\)\;
        ')
add_custom_target(make WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMAND make)
add_custom_target(makeall
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND phpize && ./configure && make)

# sources
set(SOURCE_FILES php_blackhole.h blackhole.c)

# php
set(PHP_SOURCE /usr/local/Cellar/php/7.2.9_2/include/php)
include_directories(${PHP_SOURCE}/main)
include_directories(${PHP_SOURCE}/Zend)
include_directories(${PHP_SOURCE}/sapi)
include_directories(${PHP_SOURCE}/TSRM)
include_directories(${PHP_SOURCE})
include_directories(${PROJECT_SOURCE_DIR})

# executable
add_library(${SOURCE_FILES})