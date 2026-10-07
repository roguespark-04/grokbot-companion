# Muse Charm — ESP-IDF project (Waveshare ESP32-S3-Touch-AMOLED-1.75)
# Thin-client firmware: PTT → capture → upload audio → wake Meridian → play reply → status face
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(muse_charm)
