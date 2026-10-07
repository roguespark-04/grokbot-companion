# Grok Bot Companion — ESP-IDF project (Waveshare ESP32-S3-Touch-AMOLED-1.75)
# Thin client: multi-bot touch UI, BOOT PTT / always-listen → relay → Meridian → reply audio
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.3.0")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(grokbot_companion)
