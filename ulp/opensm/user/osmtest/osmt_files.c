
/*
 * Supply missing files/routines for Windows osmtest build.
 * Point being to minimize mods to other OFED/opensm files...
 */

void OsmReportState(const char *p_str) { }

#include <..\opensm\osm_log.c>
#include <..\opensm\osm_mad_pool.c>
#include <..\opensm\osm_helper.c>
