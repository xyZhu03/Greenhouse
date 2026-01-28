# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "F:/Espressif/frameworks/esp-idf-v5.3.1/components/bootloader/subproject"
  "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader"
  "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader-prefix"
  "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader-prefix/tmp"
  "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader-prefix/src/bootloader-stamp"
  "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader-prefix/src"
  "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "F:/Programming_and_shit/SBC/invernaderoSBC/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
