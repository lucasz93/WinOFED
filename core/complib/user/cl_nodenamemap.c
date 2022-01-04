/*
 * Copyright (c) 2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2007 Lawrence Livermore National Lab
 * Copyright (c) 2009 Intel Corp., Inc.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdio.h>
#include <errno.h>
#include <_string.h>
#include <stdlib.h>

#include <complib/cl_nodenamemap.h>

// The maximum err length defined here: http://msdn.microsoft.com/en-us/library/51sah927(v=VS.80).aspx#2
#define ERR_STRING_LEN 94

static int map_name(__in void *cxt,__in uint64_t guid,__in char *p)
{
	cl_qmap_t *map = cxt;
	name_map_item_t *item;
	char * context;

	p = strtok_s(p, "\"#", &context);
	if (!p)
		return 0;

	item = malloc(sizeof(*item));
	if (!item)
		return -1;
	item->guid = guid;
	item->name = strdup(p);
	cl_qmap_insert(map, item->guid, (cl_map_item_t *)item);
	return 0;
}

nn_map_t *
open_node_name_map(char *node_name_map)
{
	nn_map_t *map;
	char err_string[ERR_STRING_LEN];
	
	if (!node_name_map) {
#ifdef HAVE_DEFAULT_NODENAME_MAP
		struct stat buf;
		node_name_map = HAVE_DEFAULT_NODENAME_MAP;
		if (stat(node_name_map, &buf))
			return NULL;
#else
		return NULL;
#endif /* HAVE_DEFAULT_NODENAME_MAP */
	}

	map = malloc(sizeof(*map));
	if (!map)
		return NULL;
	cl_qmap_init(map);

	if (parse_node_map(node_name_map, map_name, map)) {
		if (!strerror_s(err_string, ERR_STRING_LEN, errno) ) {
			fprintf(stderr,"Error: strerror_s failed\n");
		} else {
			fprintf(stderr,
				"WARNING failed to open node name map \"%s\" (%s)\n",
				node_name_map, err_string);
		}
		close_node_name_map(map);
		return NULL;
	}

	return map;
}

void
close_node_name_map(nn_map_t *map)
{
	name_map_item_t *item = NULL;

	if (!map)
		return;

	item = (name_map_item_t *)cl_qmap_head(map);
	while (item != (name_map_item_t *)cl_qmap_end(map)) {
		item = (name_map_item_t *)cl_qmap_remove(map, item->guid);
		free(item->name);
		free(item);
		item = (name_map_item_t *)cl_qmap_head(map);
	}
	free(map);
}

char *
remap_node_name(nn_map_t *map, uint64_t target_guid, char *nodedesc)
{
	char *rc = NULL;
	name_map_item_t *item = NULL;

	if (!map)
		goto done;

	item = (name_map_item_t *)cl_qmap_get(map, target_guid);
	if (item != (name_map_item_t *)cl_qmap_end(map))
		rc = strdup(item->name);

done:
	if (rc == NULL)
		rc = strdup(clean_nodedesc(nodedesc));
	return (rc);
}

char *
clean_nodedesc(char *nodedesc)
{
	int i = 0;

	nodedesc[63] = '\0';
	while (nodedesc[i]) {
		if (!isprint(nodedesc[i]))
			nodedesc[i] = ' ';
		i++;
	}

	return (nodedesc);
}

int parse_node_map(const char *file_name,
		   int (*create)(void *, uint64_t, char *), void *cxt)
{
	char line[256];
	FILE *f;
	errno_t err_no;
	char err_string[ERR_STRING_LEN];

	if (!(err_no = fopen_s(&f, file_name, "r"))) {
		if (!strerror_s(err_string, ERR_STRING_LEN, err_no) ) {
			fprintf(stderr,"Error: failed to open file \"%s\" with unknown error\n",
				file_name);
		} else {
			fprintf(stderr,
				"Error: failed to open file \"%s\" with error (%s)\n",
				file_name, err_string);
		}
		return -1;
	}

	while (fgets(line, sizeof(line), f)) {
		uint64_t guid;
		char *p, *e;

		p = line;
		while (isspace(*p))
			p++;
		if (*p == '\0' || *p == '\n' || *p == '#')
			continue;

		guid = strtoull(p, &e, 0);
		if (e == p || (!isspace(*e) && *e != '#' && *e != '\0')) {
			fclose(f);
			return -1;
		}

		p = e;
		while (isspace(*p))
			p++;

		e = strpbrk(p, "\n");
		if (e)
			*e = '\0';

		if (create(cxt, guid, p)) {
			fclose(f);
			return -1;
		}
	}

	fclose(f);
	return 0;
}
