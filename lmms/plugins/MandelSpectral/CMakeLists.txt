INCLUDE(BuildPlugin)
INCLUDE_DIRECTORIES("${CMAKE_CURRENT_SOURCE_DIR}/../MandelCommon")
BUILD_PLUGIN(mandelspectral MandelSpectral.cpp ../MandelCommon/CeciliaDsp.cpp MOCFILES MandelSpectral.h)
