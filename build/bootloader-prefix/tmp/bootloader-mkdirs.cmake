# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/root/esp/esp-idf/components/bootloader/subproject"
  "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader"
  "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader-prefix"
  "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader-prefix/tmp"
  "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader-prefix/src/bootloader-stamp"
  "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader-prefix/src"
  "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/root/ESP_Project/Lamp-ESP32-S3/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
