INCLUDE(BuildPlugin)
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/../MandelCommon")
BUILD_PLUGIN(mandelharmonizer MandelHarmonizer.cpp ../MandelCommon/CeciliaDsp.cpp MOCFILES MandelHarmonizer.h)
