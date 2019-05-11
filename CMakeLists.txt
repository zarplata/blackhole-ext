# project
cmake_minimum_required(VERSION 3.8)
project(blackhole C)

# make
add_custom_target(run
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND make &&
        php -r '
            blackhole_metric_add\(\"my_metric1\", \"localhost\", 8125\)\;
            blackhole_metric_set_tag\(\"my_metric1\", \"tag1\", \"value1\"\)\;
            blackhole_metric_set_tag\(\"my_metric1\", \"tag2\", \"value2\"\)\;

            blackhole_metric_add\(\"my_metric2\", \"localhost\", 8125\)\;
            blackhole_metric_set_tag\(\"my_metric2\", \"tag1-2\", \"value1\"\)\;
            blackhole_metric_set_tag\(\"my_metric2\", \"tag2-2\", \"value2\"\)\;

            var_dump\(blackhole_get_data\(\)\)\;
        ')
add_custom_target(make WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMAND make)
add_custom_target(makeall
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND phpize && ./configure && make)

# sources
set(SOURCE_FILES php_blackhole.h blackhole.c)

# php
set(PHP_SOURCE /usr/local/Cellar/php@7.2/7.2.15/include/php)
include_directories(${PHP_SOURCE}/main)
include_directories(${PHP_SOURCE}/Zend)
include_directories(${PHP_SOURCE}/sapi)
include_directories(${PHP_SOURCE}/TSRM)
include_directories(${PHP_SOURCE})
include_directories(${PROJECT_SOURCE_DIR})

# executable
add_library(${SOURCE_FILES})