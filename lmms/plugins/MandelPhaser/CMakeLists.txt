INCLUDE(BuildPlugin)
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/../MandelCommon")
BUILD_PLUGIN(mandelphaser MandelPhaser.cpp ../MandelCommon/CeciliaDsp.cpp MOCFILES MandelPhaser.h)
