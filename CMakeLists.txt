cmake_minimum_required(VERSION 3.16)

# SDK-Pfade setzen (optional, falls nicht global definiert)
# set(EXTRA_COMPONENT_DIRS "C:/Users/fsieb/esp/esp-zigbee-sdk/components")

# Das ESP-IDF Framework einbinden
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Den Projektnamen definieren (ERST HIER!)
project(FriColorLight)