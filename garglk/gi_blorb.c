/* gi_blorb.c: Blorb library layer for Glk API.
    gi_blorb version 1.4.
    Designed by Andrew Plotkin <erkyrath@eblong.com>
    http://eblong.com/zarf/glk/

    This file is copyright 1998-2010 by Andrew Plotkin. You may copy,
    distribute, and incorporate it into your own programs, by any means
    and under any conditions, as long as you do not modify it. You may
    also modify this file, incorporate it into your own programs,
    and distribute the modified version, as long as you retain a notice
    in your program or documentation which mentions my name and the URL
    shown above.
*/

#include <stdbool.h>
#include <stddef.h>

#include "glk.h"
#include "gi_blorb.h"

/* The magic macro of endian conversion. */

#define giblorb_native4(v)   \
    ( (((glui32)((v)[3])      ) & 0x000000ff)    \
    | (((glui32)((v)[2]) <<  8) & 0x0000ff00)    \
    | (((glui32)((v)[1]) << 16) & 0x00ff0000)    \
    | (((glui32)((v)[0]) << 24) & 0xff000000))

/* More four-byte constants. */

#define giblorb_ID_FORM (giblorb_make_id('F', 'O', 'R', 'M'))
#define giblorb_ID_IFRS (giblorb_make_id('I', 'F', 'R', 'S'))
#define giblorb_ID_RIdx (giblorb_make_id('R', 'I', 'd', 'x'))

/* giblorb_chunkdesc_t: Describes one chunk of the Blorb file. */
typedef struct giblorb_chunkdesc_struct {
    glui32 type;
    glui32 len;
    glui32 startpos; /* start of chunk header */
    glui32 datpos; /* start of data (either startpos or startpos+8) */

    void *ptr; /* pointer to malloc'd data, if loaded */
    int auxdatnum; /* entry in the auxsound/auxpict array; -1 if none.
        This only applies to chunks that represent resources;  */

} giblorb_chunkdesc_t;

/* giblorb_resdesc_t: Describes one resource in the Blorb file. */
typedef struct giblorb_resdesc_struct {
    glui32 usage;
    glui32 resnum;
    glui32 chunknum;
} giblorb_resdesc_t;

/* giblorb_map_t: Holds the complete description of an open Blorb file. */
struct giblorb_map_struct {
    glui32 inited; /* holds giblorb_Inited_Magic if the map structure is
        valid */
    strid_t file;

    int numchunks;
    giblorb_chunkdesc_t *chunks; /* list of chunk descriptors */

    int numresources;
    giblorb_resdesc_t *resources; /* list of resource descriptors */
    giblorb_resdesc_t **ressorted; /* list of pointers to descriptors
        in map->resources -- sorted by usage and resource number. */
};

#define giblorb_Inited_Magic (0xB7012BED)

/* Static variables. */

static bool lib_inited = false;

static giblorb_err_t giblorb_initialize(void);
static giblorb_err_t giblorb_initialize_map(giblorb_map_t *map);
static void giblorb_qsort(giblorb_resdesc_t **list, int len);
static giblorb_resdesc_t *giblorb_bsearch(giblorb_resdesc_t *sample,
    giblorb_resdesc_t **list, int len);
static void *giblorb_malloc(glui32 len);
static void *giblorb_realloc(void *ptr, glui32 len);
static void giblorb_free(void *ptr);

static giblorb_err_t giblorb_initialize()
{
    return giblorb_err_None;
}

giblorb_err_t giblorb_create_map(strid_t file, giblorb_map_t **newmap)
{
    giblorb_err_t err;
    giblorb_map_t *map;
    glui32 readlen;
    glui32 nextpos, totallength;
    giblorb_chunkdesc_t *chunks;
    int chunks_size, numchunks;
    char buffer[16];

    *newmap = NULL;

    if (!lib_inited) {
        err = giblorb_initialize();
        if (err)
            return err;
        lib_inited = true;
    }

    /* First, chew through the file and index the chunks. */

    glk_stream_set_position(file, 0, seekmode_Start);

    readlen = glk_get_buffer_stream(file, buffer, 12);
    if (readlen != 12)
        return giblorb_err_Read;

    if (giblorb_native4(buffer+0) != giblorb_ID_FORM)
        return giblorb_err_Format;
    if (giblorb_native4(buffer+8) != giblorb_ID_IFRS)
        return giblorb_err_Format;

    totallength = giblorb_native4(buffer+4) + 8;
    nextpos = 12;

    chunks_size = 8;
    numchunks = 0;
    chunks = (giblorb_chunkdesc_t *)giblorb_malloc(sizeof(giblorb_chunkdesc_t)
        * chunks_size);

    while (nextpos < totallength) {
        glui32 type, len;
        int chunum;
        giblorb_chunkdesc_t *chu;

        glk_stream_set_position(file, nextpos, seekmode_Start);

        readlen = glk_get_buffer_stream(file, buffer, 8);
        if (readlen != 8) {
            giblorb_free(chunks);
            return giblorb_err_Read;
        }

        type = giblorb_native4(buffer+0);
        len = giblorb_native4(buffer+4);

        if (numchunks >= chunks_size) {
            chunks_size *= 2;
            chunks = (giblorb_chunkdesc_t *)giblorb_realloc(chunks,
                sizeof(giblorb_chunkdesc_t) * chunks_size);
        }

        chunum = numchunks;
        chu = &(chunks[chunum]);
        numchunks++;

        chu->type = type;
        chu->startpos = nextpos;
        if (type == giblorb_ID_FORM) {
            chu->datpos = nextpos;
            chu->len = len+8;
        }
        else {
            chu->datpos = nextpos+8;
            chu->len = len;
        }
        chu->ptr = NULL;
        chu->auxdatnum = -1;

        nextpos = nextpos + len + 8;
        if (nextpos & 1)
            nextpos++;

        if (nextpos > totallength) {
            giblorb_free(chunks);
            return giblorb_err_Format;
        }
    }

    /* The basic IFF structure seems to be ok, and we have a list of
        chunks. Now we allocate the map structure itself. */

    map = (giblorb_map_t *)giblorb_malloc(sizeof(giblorb_map_t));
    if (!map) {
        giblorb_free(chunks);
        return giblorb_err_Alloc;
    }

    map->inited = giblorb_Inited_Magic;
    map->file = file;
    map->chunks = chunks;
    map->numchunks = numchunks;
    map->resources = NULL;
    map->ressorted = NULL;
    map->numresources = 0;
    /*map->releasenum = 0;
    map->zheader = NULL;
    map->resolution = NULL;
    map->palettechunk = -1;
    map->palette = NULL;
    map->auxsound = NULL;
    map->auxpict = NULL;*/

    /* Now we do everything else involved in loading the Blorb file,
        such as building resource lists. */

    err = giblorb_initialize_map(map);
    if (err) {
        giblorb_destroy_map(map);
        return err;
    }

    *newmap = map;
    return giblorb_err_None;
}

static giblorb_err_t giblorb_initialize_map(giblorb_map_t *map)
{
    /* It is important that the map structure be kept valid during this
        function. If this returns an error, giblorb_destroy_map() will
        be called. */

    int ix, jx;
    giblorb_result_t chunkres;
    giblorb_err_t err;
    char *ptr;
    glui32 len;
    glui32 numres;
    bool gotindex = false;

    for (ix=0; ix<map->numchunks; ix++) {
        giblorb_chunkdesc_t *chu = &map->chunks[ix];

        switch (chu->type) {

            case giblorb_ID_RIdx:
                /* Resource index chunk: build the resource list and
                sort it. */

                if (gotindex)
                    return giblorb_err_Format; /* duplicate index chunk */
                err = giblorb_load_chunk_by_number(map, giblorb_method_Memory,
                    &chunkres, ix);
                if (err)
                    return err;

                ptr = chunkres.data.ptr;
                len = chunkres.length;
                numres = giblorb_native4(ptr+0);

                if (numres) {
                    int ix2;
                    giblorb_resdesc_t *resources;
                    giblorb_resdesc_t **ressorted;

                    if (len != numres*12+4)
                        return giblorb_err_Format; /* bad length field */

                    resources = (giblorb_resdesc_t *)giblorb_malloc(numres
                        * sizeof(giblorb_resdesc_t));
                    ressorted = (giblorb_resdesc_t **)giblorb_malloc(numres
                        * sizeof(giblorb_resdesc_t *));
                    if (!ressorted || !resources) {
                        giblorb_free(resources);
                        giblorb_free(ressorted);
                        return giblorb_err_Alloc;
                    }

                    ix2 = 0;
                    for (jx=0; jx<numres; jx++) {
                        giblorb_resdesc_t *res = &(resources[jx]);
                        glui32 respos;

                        res->usage = giblorb_native4(ptr+jx*12+4);
                        res->resnum = giblorb_native4(ptr+jx*12+8);
                        respos = giblorb_native4(ptr+jx*12+12);

                        while (ix2 < map->numchunks
                            && map->chunks[ix2].startpos < respos)
                            ix2++;

                        if (ix2 >= map->numchunks
                                || map->chunks[ix2].startpos != respos) {
                            giblorb_free(resources);
                            giblorb_free(ressorted);
                            return giblorb_err_Format; /* start pos does
                                                          not match a real chunk */
                        }

                        res->chunknum = ix2;

                        ressorted[jx] = res;
                    }

                    /* Sort a resource list (actually a list of pointers to
                        structures in map->resources.) This makes it easy
                        to find resources by usage and resource number. */
                    giblorb_qsort(ressorted, numres);

                    map->numresources = numres;
                    map->resources = resources;
                    map->ressorted = ressorted;
                }

                giblorb_unload_chunk(map, ix);
                gotindex = true;
                break;

        }
    }

    return giblorb_err_None;
}

giblorb_err_t giblorb_destroy_map(giblorb_map_t *map)
{
    int ix;

    if (!map || !map->chunks || map->inited != giblorb_Inited_Magic)
        return giblorb_err_NotAMap;

    for (ix=0; ix<map->numchunks; ix++) {
        giblorb_chunkdesc_t *chu = &(map->chunks[ix]);
        if (chu->ptr) {
            giblorb_free(chu->ptr);
            chu->ptr = NULL;
        }
    }

    if (map->chunks) {
        giblorb_free(map->chunks);
        map->chunks = NULL;
    }

    map->numchunks = 0;

    if (map->resources) {
        giblorb_free(map->resources);
        map->resources = NULL;
    }

    if (map->ressorted) {
        giblorb_free(map->ressorted);
        map->ressorted = NULL;
    }

    map->numresources = 0;

    map->file = NULL;
    map->inited = 0;

    giblorb_free(map);

    return giblorb_err_None;
}

/* Chunk-handling functions. */

giblorb_err_t giblorb_load_chunk_by_type(giblorb_map_t *map,
    glui32 method, giblorb_result_t *res, glui32 type,
    glui32 count)
{
    int ix;

    for (ix=0; ix < map->numchunks; ix++) {
        if (map->chunks[ix].type == type) {
            if (count == 0)
                break;
            count--;
        }
    }

    if (ix >= map->numchunks) {
        return giblorb_err_NotFound;
    }

    return giblorb_load_chunk_by_number(map, method, res, ix);
}

giblorb_err_t giblorb_load_chunk_by_number(giblorb_map_t *map,
    glui32 method, giblorb_result_t *res, glui32 chunknum)
{
    giblorb_chunkdesc_t *chu;

    if (chunknum < 0 || chunknum >= map->numchunks)
        return giblorb_err_NotFound;

    chu = &(map->chunks[chunknum]);

    switch (method) {

        case giblorb_method_DontLoad:
            /* do nothing */
            break;

        case giblorb_method_FilePos:
            res->data.startpos = chu->datpos;
            break;

        case giblorb_method_Memory:
            if (!chu->ptr) {
                glui32 readlen;
                void *dat = giblorb_malloc(chu->len);

                if (!dat)
                    return giblorb_err_Alloc;

                glk_stream_set_position(map->file, chu->datpos,
                    seekmode_Start);

                readlen = glk_get_buffer_stream(map->file, dat,
                    chu->len);
                if (readlen != chu->len)
                    return giblorb_err_Read;

                chu->ptr = dat;
            }
            res->data.ptr = chu->ptr;
            break;
    }

    res->chunknum = chunknum;
    res->length = chu->len;
    res->chunktype = chu->type;

    return giblorb_err_None;
}

giblorb_err_t giblorb_load_resource(giblorb_map_t *map, glui32 method,
    giblorb_result_t *res, glui32 usage, glui32 resnum)
{
    giblorb_resdesc_t sample;
    giblorb_resdesc_t *found;

    sample.usage = usage;
    sample.resnum = resnum;

    found = giblorb_bsearch(&sample, map->ressorted, map->numresources);

    if (!found)
        return giblorb_err_NotFound;

    return giblorb_load_chunk_by_number(map, method, res, found->chunknum);
}

giblorb_err_t giblorb_unload_chunk(giblorb_map_t *map, glui32 chunknum)
{
    giblorb_chunkdesc_t *chu;

    if (chunknum < 0 || chunknum >= map->numchunks)
        return giblorb_err_NotFound;

    chu = &(map->chunks[chunknum]);

    if (chu->ptr) {
        giblorb_free(chu->ptr);
        chu->ptr = NULL;
    }

    return giblorb_err_None;
}

giblorb_err_t giblorb_count_resources(giblorb_map_t *map, glui32 usage,
    glui32 *num, glui32 *min, glui32 *max)
{
    int ix;
    int count;
    glui32 val;
    glui32 minval, maxval;

    count = 0;
    minval = 0;
    maxval = 0;

    for (ix=0; ix<map->numresources; ix++) {
        if (map->resources[ix].usage == usage) {
            val = map->resources[ix].resnum;
            if (count == 0) {
                count++;
                minval = val;
                maxval = val;
            }
            else {
                count++;
                if (val < minval)
                    minval = val;
                if (val > maxval)
                    maxval = val;
            }
        }
    }

    if (num)
        *num = count;
    if (min)
        *min = minval;
    if (max)
        *max = maxval;

    return giblorb_err_None;
}

/* Sorting and searching. */

static int sortsplot(giblorb_resdesc_t *v1, giblorb_resdesc_t *v2)
{
    if (v1->usage < v2->usage)
        return -1;
    if (v1->usage > v2->usage)
        return 1;
    if (v1->resnum < v2->resnum)
        return -1;
    if (v1->resnum > v2->resnum)
        return 1;
    return 0;
}

static void giblorb_qsort(giblorb_resdesc_t **list, int len)
{
    int ix, jx, res;
    giblorb_resdesc_t *tmpptr, *pivot;

    if (len < 6) {
        /* The list is short enough for a bubble-sort. */
        for (jx=len-1; jx>0; jx--) {
            for (ix=0; ix<jx; ix++) {
                res = sortsplot(list[ix], list[ix+1]);
                if (res > 0) {
                    tmpptr = list[ix];
                    list[ix] = list[ix+1];
                    list[ix+1] = tmpptr;
                }
            }
        }
    }
    else {
        /* Split the list. */
        pivot = list[len/2];
        ix=0;
        jx=len;
        while (1) {
            while (ix < jx-1 && sortsplot(list[ix], pivot) < 0)
                ix++;
            while (ix < jx-1 && sortsplot(list[jx-1], pivot) > 0)
                jx--;
            if (ix >= jx-1)
                break;
            tmpptr = list[ix];
            list[ix] = list[jx-1];
            list[jx-1] = tmpptr;
        }
        ix++;
        /* Sort the halves. */
        giblorb_qsort(list+0, ix);
        giblorb_qsort(list+ix, len-ix);
    }
}

giblorb_resdesc_t *giblorb_bsearch(giblorb_resdesc_t *sample,
    giblorb_resdesc_t **list, int len)
{
    int top, bot, val, res;

    bot = 0;
    top = len;

    while (bot < top) {
        val = (top+bot) / 2;
        res = sortsplot(list[val], sample);
        if (res == 0)
            return list[val];
        if (res < 0) {
            bot = val+1;
        }
        else {
            top = val;
        }
    }

    return NULL;
}


/* Boring utility functions. If your platform doesn't support ANSI
    malloc(), feel free to edit these however you like. */

#include <stdlib.h> /* The OS-native header file -- you can edit
    this too. */

static void *giblorb_malloc(glui32 len)
{
    return malloc(len);
}

static void *giblorb_realloc(void *ptr, glui32 len)
{
    return realloc(ptr, len);
}

static void giblorb_free(void *ptr)
{
    free(ptr);
}
