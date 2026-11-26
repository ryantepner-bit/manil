INCLUDE(BuildPlugin)
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/../MandelCommon")
BUILD_PLUGIN(mandelfreqshift MandelFreqShift.cpp ../MandelCommon/CeciliaDsp.cpp MOCFILES MandelFreqShift.h)
