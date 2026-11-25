INCLUDE(BuildPlugin)
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/../MandelCommon")
BUILD_PLUGIN(mandelresonator MandelResonator.cpp ../MandelCommon/CeciliaDsp.cpp MOCFILES MandelResonator.h)
