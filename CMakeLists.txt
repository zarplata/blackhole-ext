# project
cmake_minimum_required(VERSION 3.8)
project(blackhole C)

# make
add_custom_target(run
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND make &&
        php -r '
            blackhole_set_tag\(\"controller\", \"main\"\)\;
            blackhole_set_tag\(\"action\", \"index\"\)\;
            blackhole_set_metric_name\(\"this_is_my_metric\"\)\;
            echo blackhole_get_data\(\), PHP_EOL\;
            var_dump\(blackhole_get_info\(\)\)\;
        ')
add_custom_target(make WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} COMMAND make)
add_custom_target(makeall
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
        COMMAND phpize && ./configure -enable-debug && make)

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