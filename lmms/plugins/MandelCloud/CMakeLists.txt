INCLUDE(BuildPlugin)
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/../MandelCommon")
BUILD_PLUGIN(mandelcloud MandelCloud.cpp ../MandelCommon/CeciliaDsp.cpp MOCFILES MandelCloud.h)
