/*
 * Copyright © 2002 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * A slightly modified version of XCursor used with Wayland only builds.
 */
#pragma once

#ifndef ICONDIR
#define ICONDIR "/usr/X11R6/lib/X11/icons"
#endif

#ifndef XCURSORPATH
#define XCURSORPATH "~/.local/share/icons:~/.icons:/usr/share/icons:/usr/share/pixmaps:"ICONDIR
#endif

#define XCURSOR_SCAN_CORE   ((FILE *) 1)
#define XCURSOR_CORE_THEME  "core"
#define MAX_INHERITS_DEPTH  32

static const unsigned short _XcursorStandardNameOffsets[] = {
	0, 9, 15, 32, 47, 52, 61, 80, 100, 112, 123, 134, 145, 152, 158,
	169, 175, 189, 199, 213, 217, 224, 237, 249, 261, 272, 281, 287,
	295, 301, 307, 313, 319, 324, 335, 344, 354, 363, 374, 383, 392,
	396, 409, 415, 422, 429, 434, 449, 459, 470, 480, 492, 501, 510,
	524, 542, 556, 571, 583, 601, 609, 616, 623, 632, 637, 644, 651,
	666, 682, 699, 708, 716, 721, 730, 739, 748, 754
};

#define NUM_STANDARD_NAMES  (sizeof _XcursorStandardNameOffsets / sizeof _XcursorStandardNameOffsets[0])

#define STANDARD_NAME(id) \
    _XcursorStandardNames + _XcursorStandardNameOffsets[id]

#define XcursorWhite(c)	((c) == ' ' || (c) == '\t' || (c) == '\n')
#define XcursorSep(c) ((c) == ';' || (c) == ',')

static const char _XcursorStandardNames[] =
	"X_cursor\0"
	"arrow\0"
	"based_arrow_down\0"
	"based_arrow_up\0"
	"boat\0"
	"bogosity\0"
	"bottom_left_corner\0"
	"bottom_right_corner\0"
	"bottom_side\0"
	"bottom_tee\0"
	"box_spiral\0"
	"center_ptr\0"
	"circle\0"
	"clock\0"
	"coffee_mug\0"
	"cross\0"
	"cross_reverse\0"
	"crosshair\0"
	"diamond_cross\0"
	"dot\0"
	"dotbox\0"
	"double_arrow\0"
	"draft_large\0"
	"draft_small\0"
	"draped_box\0"
	"exchange\0"
	"fleur\0"
	"gobbler\0"
	"gumby\0"
	"hand1\0"
	"hand2\0"
	"heart\0"
	"icon\0"
	"iron_cross\0"
	"left_ptr\0"
	"left_side\0"
	"left_tee\0"
	"leftbutton\0"
	"ll_angle\0"
	"lr_angle\0"
	"man\0"
	"middlebutton\0"
	"mouse\0"
	"pencil\0"
	"pirate\0"
	"plus\0"
	"question_arrow\0"
	"right_ptr\0"
	"right_side\0"
	"right_tee\0"
	"rightbutton\0"
	"rtl_logo\0"
	"sailboat\0"
	"sb_down_arrow\0"
	"sb_h_double_arrow\0"
	"sb_left_arrow\0"
	"sb_right_arrow\0"
	"sb_up_arrow\0"
	"sb_v_double_arrow\0"
	"shuttle\0"
	"sizing\0"
	"spider\0"
	"spraycan\0"
	"star\0"
	"target\0"
	"tcross\0"
	"top_left_arrow\0"
	"top_left_corner\0"
	"top_right_corner\0"
	"top_side\0"
	"top_tee\0"
	"trek\0"
	"ul_angle\0"
	"umbrella\0"
	"ur_angle\0"
	"watch\0"
	"xterm";

/*
 * Cursor files start with a header.  The header
 * contains a magic number, a version number and a
 * table of contents which has type and offset information
 * for the remaining tables in the file.
 *
 * File minor versions increment for compatible changes
 * File major versions increment for incompatible changes (never, we hope)
 *
 * Chunks of the same type are always upward compatible.  Incompatible
 * changes are made with new chunk types; the old data can remain under
 * the old type.  Upward compatible changes can add header data as the
 * header lengths are specified in the file.
 *
 *  File:
 *	FileHeader
 *	LISTofChunk
 *
 *  FileHeader:
 *	CARD32		magic	    magic number
 *	CARD32		header	    bytes in file header
 *	CARD32		version	    file version
 *	CARD32		ntoc	    number of toc entries
 *	LISTofFileToc   toc	    table of contents
 *
 *  FileToc:
 *	CARD32		type	    entry type
 *	CARD32		subtype	    entry subtype (size for images)
 *	CARD32		position    absolute file position
 */

#define XCURSOR_MAGIC	0x72756358  /* "Xcur" LSBFirst */

/*
 * The rest of the file is a list of chunks, each tagged by type
 * and version.
 *
 *  Chunk:
 *	ChunkHeader
 *	<extra type-specific header fields>
 *	<type-specific data>
 *
 *  ChunkHeader:
 *	CARD32	    header	bytes in chunk header + type header
 *	CARD32	    type	chunk type
 *	CARD32	    subtype	chunk subtype
 *	CARD32	    version	chunk type version
 */
#define XCURSOR_CHUNK_HEADER_LEN    (4 * 4)

/*
 * This version number is stored in cursor files; changes to the
 * file format require updating this version number
 */
#define XCURSOR_FILE_MAJOR	1
#define XCURSOR_FILE_MINOR	0
#define XCURSOR_FILE_VERSION	((XCURSOR_FILE_MAJOR << 16) | (XCURSOR_FILE_MINOR))
#define XCURSOR_FILE_HEADER_LEN	(4 * 4)
#define XCURSOR_FILE_TOC_LEN	(3 * 4)
/*
 * Here's a list of the known chunk types
 */

/*
 * Comments consist of a 4-byte length field followed by
 * UTF-8 encoded text
 *
 *  Comment:
 *	ChunkHeader header	chunk header
 *	CARD32	    length	bytes in text
 *	LISTofCARD8 text	UTF-8 encoded text
 */

#define XCURSOR_COMMENT_TYPE	    0xfffe0001
#define XCURSOR_COMMENT_VERSION	    1
#define XCURSOR_COMMENT_HEADER_LEN  (XCURSOR_CHUNK_HEADER_LEN + (1 *4))
#define XCURSOR_COMMENT_COPYRIGHT   1
#define XCURSOR_COMMENT_LICENSE	    2
#define XCURSOR_COMMENT_OTHER	    3
#define XCURSOR_COMMENT_MAX_LEN	    0x100000

/*
 * Each cursor image occupies a separate image chunk.
 * The length of the image header follows the chunk header
 * so that future versions can extend the header without
 * breaking older applications
 *
 *  Image:
 *	ChunkHeader	header	chunk header
 *	CARD32		width	actual width
 *	CARD32		height	actual height
 *	CARD32		xhot	hot spot x
 *	CARD32		yhot	hot spot y
 *	CARD32		delay	animation delay
 *	LISTofCARD32	pixels	ARGB pixels
 */
#define XCURSOR_IMAGE_TYPE    	    0xfffd0002
#define XCURSOR_IMAGE_VERSION	    1
#define XCURSOR_IMAGE_MAX_SIZE	    0x7fff	/* 32767x32767 max cursor size */

typedef struct _XcursorFileToc {
    unsigned int	type;	/* chunk type */
    unsigned int	subtype;	/* subtype (size for images) */
    unsigned int	position;	/* absolute position in file */
} XcursorFileToc;

typedef struct _XcursorFileHeader {
    unsigned int	 magic;	/* magic number */
    unsigned int	 header;	/* byte length of header */
    unsigned int	 version;	/* file version number */
    unsigned int	 ntoc;	/* number of toc entries */
    XcursorFileToc	*tocs;	/* table of contents */
} XcursorFileHeader;

typedef struct _XcursorChunkHeader {
    unsigned int	header;	/* bytes in chunk header */
    unsigned int	type;	/* chunk type */
    unsigned int	subtype;	/* chunk subtype (size for images) */
    unsigned int	version;	/* version of this type */
} XcursorChunkHeader;

typedef struct _XcursorComment {
    unsigned int	version;
    unsigned int	comment_type;
    char	       *comment;
} XcursorComment;

typedef struct _XcursorFile XcursorFile;

struct _XcursorFile {
    void	*closure;
    int	    (*read)  (XcursorFile *file, unsigned char *buf, int len);
    int	    (*write) (XcursorFile *file, unsigned char *buf, int len);
    int	    (*seek)  (XcursorFile *file, long offset, int whence);
};

typedef struct _XcursorComments {
    int		    	  ncomment;	/* number of comments */
    XcursorComment	**comments;	/* array of XcursorComment pointers */
} XcursorComments;

typedef struct _XcursorInherit {
    char		*line;
    const char	*theme;
} XcursorInherit;


typedef unsigned int	XcursorPixel;

typedef struct _XcursorImage {
    unsigned int	version;	/* version of the image data */
    unsigned int	size;	/* nominal size for matching */
    unsigned int	width;	/* actual width */
    unsigned int	height;	/* actual height */
    unsigned int	xhot;	/* hot spot x (must be inside image) */
    unsigned int	yhot;	/* hot spot y (must be inside image) */
    unsigned int	delay;	/* animation delay to next frame (ms) */
    XcursorPixel   *pixels;	/* pointer to pixels */
} XcursorImage;

/*
 * Other data structures exposed by the library API
 */
typedef struct _XcursorImages {
    int			   nimage;	/* number of images */
    XcursorImage **images;	/* array of XcursorImage pointers */
    char		  *name;	/* name used to load images */
} XcursorImages;

void XcursorImagesDestroy (XcursorImages *images);

XcursorImages * XcursorImagesCreate (int size);

XcursorImages * XcursorLibraryLoadImages (const char *library,
						  				  const char *theme, 
						  				  int 		  size);

XcursorImage * XcursorImageCreate (int width, int height);

void XcursorImageDestroy (XcursorImage *image);
