#include "geomalembicreader.h"

using namespace VR;

#define GeomAlembicReader_PluginID PluginID(LARGE_CONST(2017051657))
static VRAY3_CONST_COMPAT tchar *libText = "Alembic reader for V-Ray";
static const tchar *descText = "A more advanced .vrmesh/Alembic reader for V-Ray";
#ifdef __VRAY40__
SIMPLE_PLUGIN_LIBRARY(GeomAlembicReader_PluginID, "GeomAlembicReader", libText, descText, GeomAlembicReader, GeomAlembicReader_Params, EXT_STATIC_GEOM_SOURCE);
#else
SIMPLE_PLUGIN_LIBRARY(GeomAlembicReader_PluginID, EXT_STATIC_GEOM_SOURCE, "GeomAlembicReader", libText, GeomAlembicReader, GeomAlembicReader_Params);
#endif
