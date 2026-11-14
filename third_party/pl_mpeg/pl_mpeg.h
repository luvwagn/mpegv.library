/*
PL_MPEG - MPEG1 Video decoder, MP2 Audio decoder, MPEG-PS demuxer
SPDX-License-Identifier: MIT

Dominic Szablewski - https://phoboslab.org


-- Synopsis

// Define `PL_MPEG_IMPLEMENTATION` in *one* C/C++ file before including this
// library to create the implementation.

#define PL_MPEG_IMPLEMENTATION
#include "plmpeg.h"

// This function gets called for each decoded video frame
void my_video_callback(plm_t *plm, plm_frame_t *frame, void *user) {
	// Do something with frame->y.data, frame->cr.data, frame->cb.data
}

// This function gets called for each decoded audio frame
void my_audio_callback(plm_t *plm, plm_samples_t *frame, void *user) {
	// Do something with samples->interleaved
}

// Load a .mpg (MPEG Program Stream) file
plm_t *plm = plm_create_with_filename("some-file.mpg");

// Install the video & audio decode callbacks
plm_set_video_decode_callback(plm, my_video_callback, my_data);
plm_set_audio_decode_callback(plm, my_audio_callback, my_data);


// Decode
do {
	plm_decode(plm, time_since_last_call);
} while (!plm_has_ended(plm));

// All done
plm_destroy(plm);



-- Documentation

This library provides several interfaces to load, demux and decode MPEG video
and audio data. A high-level API combines the demuxer, video & audio decoders
in an easy to use wrapper.

Lower-level APIs for accessing the demuxer, video decoder and audio decoder, 
as well as providing different data sources are also available.

Interfaces are written in an object oriented style, meaning you create object 
instances via various different constructor functions (plm_*create()),
do some work on them and later dispose them via plm_*destroy().

plm_* ......... the high-level interface, combining demuxer and decoders
plm_buffer_* .. the data source used by all interfaces
plm_demux_* ... the MPEG-PS demuxer
plm_video_* ... the MPEG1 Video ("mpeg1") decoder
plm_audio_* ... the MPEG1 Audio Layer II ("mp2") decoder


With the high-level interface you have two options to decode video & audio:

 1. Use plm_decode() and just hand over the delta time since the last call.
    It will decode everything needed and call your callbacks (specified through
    plm_set_{video|audio}_decode_callback()) any number of times.

 2. Use plm_decode_video() and plm_decode_audio() to decode exactly one
    frame of video or audio data at a time. How you handle the synchronization 
    of both streams is up to you.

If you only want to decode video *or* audio through these functions, you should
disable the other stream (plm_set_{video|audio}_enabled(FALSE))

Video data is decoded into a struct with all 3 planes (Y, Cr, Cb) stored in
separate buffers. You can either convert this to RGB on the CPU (slow) via the
plm_frame_to_rgb() function or do it on the GPU with the following matrix:

mat4 bt601 = mat4(
	1.16438,  0.00000,  1.59603, -0.87079,
	1.16438, -0.39176, -0.81297,  0.52959,
	1.16438,  2.01723,  0.00000, -1.08139,
	0, 0, 0, 1
);
gl_FragColor = vec4(y, cb, cr, 1.0) * bt601;

Audio data is decoded into a struct with either one single float array with the
samples for the left and right channel interleaved, or if the 
PLM_AUDIO_SEPARATE_CHANNELS is defined *before* including this library, into
two separate float arrays - one for each channel.


Data can be supplied to the high level interface, the demuxer and the decoders
in three different ways:

 1. Using plm_create_from_filename() or with a file handle with 
    plm_create_from_file().

 2. Using plm_create_with_memory() and supplying a pointer to memory that
    contains the whole file.

 3. Using plm_create_with_buffer(), supplying your own plm_buffer_t instance and
    periodically writing to this buffer.

When using your own plm_buffer_t instance, you can fill this buffer using 
plm_buffer_write(). You can either monitor plm_buffer_get_remaining() and push 
data when appropriate, or install a callback on the buffer with 
plm_buffer_set_load_callback() that gets called whenever the buffer needs more 
data.

A buffer created with plm_buffer_create_with_capacity() is treated as a ring
buffer, meaning that data that has already been read, will be discarded. In
contrast, a buffer created with plm_buffer_create_for_appending() will keep all
data written to it in memory. This enables seeking in the already loaded data.


There should be no need to use the lower level plm_demux_*, plm_video_* and 
plm_audio_* functions, if all you want to do is read/decode an MPEG-PS file.
However, if you get raw mpeg1video data or raw mp2 audio data from a different
source, these functions can be used to decode the raw data directly. Similarly, 
if you only want to analyze an MPEG-PS file or extract raw video or audio
packets from it, you can use the plm_demux_* functions.


This library uses malloc(), realloc() and free() to manage memory. Typically 
all allocation happens up-front when creating the interface. However, the
default buffer size may be too small for certain inputs. In these cases plmpeg
will realloc() the buffer with a larger size whenever needed. You can configure
the default buffer size by defining PLM_BUFFER_DEFAULT_SIZE *before* 
including this library.

You can also define PLM_MALLOC, PLM_REALLOC and PLM_FREE to provide your own
memory management functions.


See below for detailed the API documentation.

*/


#ifndef PL_MPEG_H
#define PL_MPEG_H

#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Public Data Types


// Object types for the various interfaces

typedef struct plm_buffer_t plm_buffer_t;
typedef struct plm_demux_t plm_demux_t;
typedef struct plm_video_t plm_video_t;


// Demuxed MPEG PS packet
// The type maps directly to the various MPEG-PES start codes. PTS is the
// presentation time stamp of the packet in seconds. Note that not all packets
// have a PTS value, indicated by PLM_PACKET_INVALID_TS.

#define PLM_PACKET_INVALID_TS -1

typedef struct {
	int type;
	size_t length;
	uint8_t *data;
	int64_t pts90;	// mpegv: the PTS as it is stored (90 kHz), -1 none
} plm_packet_t;


// Decoded Video Plane 
// The byte length of the data is width * height. Note that different planes
// have different sizes: the Luma plane (Y) is double the size of each of 
// the two Chroma planes (Cr, Cb) - i.e. 4 times the byte length.
// Also note that the size of the plane does *not* denote the size of the 
// displayed frame. The sizes of planes are always rounded up to the nearest
// macroblock (16px).

typedef struct {
	unsigned int width;
	unsigned int height;
	uint8_t *data;
} plm_plane_t;


// Decoded Video Frame
// width and height denote the desired display size of the frame. This may be
// different from the internal size of the 3 planes.

typedef struct {
	unsigned int width;
	unsigned int height;
	plm_plane_t y;
	plm_plane_t cr;
	plm_plane_t cb;
	unsigned int index;	// mpegv: its number in display order (B pictures skipped counted)
	int type;			// mpegv: 1 I, 2 P, 3 B
} plm_frame_t;


// Callback function for plm_buffer when it needs more data

typedef void(*plm_buffer_load_callback)(plm_buffer_t *self, void *user);


// Callback function for plm_buffer when it needs to seek

typedef void(*plm_buffer_seek_callback)(plm_buffer_t *self, size_t offset, void *user);


// Callback function for plm_buffer when it needs to tell the position

typedef size_t(*plm_buffer_tell_callback)(plm_buffer_t *self, void *user);


// -----------------------------------------------------------------------------
// plm_buffer public API
// Provides the data source for all other plm_* interfaces


// The default size for buffers created from files or by the high-level API

#ifndef PLM_BUFFER_DEFAULT_SIZE
#define PLM_BUFFER_DEFAULT_SIZE (128 * 1024)
#endif

#ifndef PLM_NO_STDIO

// Create a buffer instance with a filename. Returns NULL if the file could not
// be opened.

plm_buffer_t *plm_buffer_create_with_filename(const char *filename);


// Create a buffer instance with a file handle. Pass TRUE to close_when_done
// to let plmpeg call fclose() on the handle when plm_destroy() is called.

plm_buffer_t *plm_buffer_create_with_file(FILE *fh, int close_when_done);

#endif // PLM_NO_STDIO


// Create a buffer instance with custom callbacks for loading, seeking and
// telling the position. This behaves like a file handle, but with user-defined
// callbacks, useful for file handles that don't use the standard FILE API.
// Setting the length and closing/freeing has to be done manually.

plm_buffer_t *plm_buffer_create_with_callbacks(
	plm_buffer_load_callback load_callback,
	plm_buffer_seek_callback seek_callback,
	plm_buffer_tell_callback tell_callback,
	size_t length,
	void *user
);


// Create a buffer instance with a pointer to memory as source. This assumes
// the whole file is in memory. The bytes are not copied. Pass 1 to 
// free_when_done to let plmpeg call free() on the pointer when plm_destroy() 
// is called.

plm_buffer_t *plm_buffer_create_with_memory(uint8_t *bytes, size_t length, int free_when_done);


// Create an empty buffer with an initial capacity. The buffer will grow
// as needed. Data that has already been read, will be discarded.

plm_buffer_t *plm_buffer_create_with_capacity(size_t capacity);


// Create an empty buffer with an initial capacity. The buffer will grow
// as needed. Decoded data will *not* be discarded. This can be used when
// loading a file over the network, without needing to throttle the download. 
// It also allows for seeking in the already loaded data.

plm_buffer_t *plm_buffer_create_for_appending(size_t initial_capacity);


// Destroy a buffer instance and free all data

void plm_buffer_destroy(plm_buffer_t *self);


// Copy data into the buffer. If the data to be written is larger than the 
// available space, the buffer will realloc() with a larger capacity. 
// Returns the number of bytes written. This will always be the same as the
// passed in length, except when the buffer was created _with_memory() for
// which _write() is forbidden.

size_t plm_buffer_write(plm_buffer_t *self, uint8_t *bytes, size_t length);


// Mark the current byte length as the end of this buffer and signal that no 
// more data is expected to be written to it. This function should be called
// just after the last plm_buffer_write().
// For _with_capacity buffers, this is cleared on a plm_buffer_rewind().

void plm_buffer_signal_end(plm_buffer_t *self);


// Set a callback that is called whenever the buffer needs more data

void plm_buffer_set_load_callback(plm_buffer_t *self, plm_buffer_load_callback fp, void *user);


// Rewind the buffer back to the beginning. When loading from a file handle,
// this also seeks to the beginning of the file.

void plm_buffer_rewind(plm_buffer_t *self);


// Get the total size. For files, this returns the file size. For all other 
// types it returns the number of bytes currently in the buffer.

size_t plm_buffer_get_size(plm_buffer_t *self);


// Get the number of remaining (yet unread) bytes in the buffer. This can be
// useful to throttle writing.

size_t plm_buffer_get_remaining(plm_buffer_t *self);


// Get whether the read position of the buffer is at the end and no more data 
// is expected.

int plm_buffer_has_ended(plm_buffer_t *self);



// -----------------------------------------------------------------------------
// plm_demux public API
// Demux an MPEG Program Stream (PS) data into separate packages


// Various Packet Types

static const int PLM_DEMUX_PACKET_PRIVATE = 0xBD;
static const int PLM_DEMUX_PACKET_AUDIO_1 = 0xC0;
static const int PLM_DEMUX_PACKET_AUDIO_2 = 0xC1;
static const int PLM_DEMUX_PACKET_AUDIO_3 = 0xC2;
static const int PLM_DEMUX_PACKET_AUDIO_4 = 0xC3;
static const int PLM_DEMUX_PACKET_VIDEO_1 = 0xE0;


// Create a demuxer with a plm_buffer as source. This will also attempt to read
// the pack and system headers from the buffer.

plm_demux_t *plm_demux_create(plm_buffer_t *buffer, int destroy_when_done);


// Destroy a demuxer and free all data.

void plm_demux_destroy(plm_demux_t *self);


// Returns TRUE/FALSE whether pack and system headers have been found. This will
// attempt to read the headers if non are present yet.

int plm_demux_has_headers(plm_demux_t *self);


// Probe the file for the actual number of video/audio streams. See
// plm_probe() for the details.

int plm_demux_probe(plm_demux_t *self, size_t probesize);


// Returns the number of video streams found in the system header. This will
// attempt to read the system header if non is present yet.

int plm_demux_get_num_video_streams(plm_demux_t *self);


// Returns the number of audio streams found in the system header. This will
// attempt to read the system header if non is present yet.

int plm_demux_get_num_audio_streams(plm_demux_t *self);


// Rewind the internal buffer. See plm_buffer_rewind().

void plm_demux_rewind(plm_demux_t *self);


// Get whether the file has ended. This will be cleared on seeking or rewind.

int plm_demux_has_ended(plm_demux_t *self);








// Decode and return the next packet. The returned packet_t is valid until
// the next call to plm_demux_decode() or until the demuxer is destroyed.

plm_packet_t *plm_demux_decode(plm_demux_t *self);



// -----------------------------------------------------------------------------
// plm_video public API
// Decode MPEG1 Video ("mpeg1") data into raw YCrCb frames


// Create a video decoder with a plm_buffer as source.

plm_video_t *plm_video_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done);


// Destroy a video decoder and free all data.

void plm_video_destroy(plm_video_t *self);


// Get whether a sequence header was found and we can accurately report on
// dimensions and framerate.

int plm_video_has_header(plm_video_t *self);


// Get the display width/height.

int plm_video_get_width(plm_video_t *self);
int plm_video_get_height(plm_video_t *self);


// Set "no delay" mode. When enabled, the decoder assumes that the video does
// *not* contain any B-Frames. This is useful for reducing lag when streaming.
// The default is FALSE.

void plm_video_set_no_delay(plm_video_t *self, int no_delay);


// AmigaMCE: skip B pictures (when the CPU is behind). Nothing is predicted
// from a B picture, so it can be passed over undecoded: plm_video_decode()
// goes on to the next picture, the time still advanced for the one passed.
// How many have been passed over so far.

void plm_video_set_skip_b(plm_video_t *self, int skip);
unsigned int plm_video_get_skipped_b(plm_video_t *self);






// Rewind the internal buffer. See plm_buffer_rewind().

void plm_video_rewind(plm_video_t *self);


// Get whether the file has ended. This will be cleared on rewind.

int plm_video_has_ended(plm_video_t *self);


// Decode and return one frame of video and advance the internal time by 
// 1/framerate seconds. The returned frame_t is valid until the next call of
// plm_video_decode() or until the video decoder is destroyed.

plm_frame_t *plm_video_decode(plm_video_t *self);


// Convert the YCrCb data of a frame into interleaved R G B data. The stride
// specifies the width in bytes of the destination buffer. I.e. the number of
// bytes from one line to the next. The stride must be at least 
// (frame->width * bytes_per_pixel). The buffer pointed to by *dest must have a
// size of at least (stride * frame->height).
// Note that the alpha component of the dest buffer is always left untouched.

void plm_frame_to_rgb(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_bgr(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_rgba(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_bgra(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_argb(plm_frame_t *frame, uint8_t *dest, int stride);
void plm_frame_to_abgr(plm_frame_t *frame, uint8_t *dest, int stride);


#ifdef __cplusplus
}
#endif

#endif // PL_MPEG_H



// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// IMPLEMENTATION

#ifdef PL_MPEG_IMPLEMENTATION

#include <string.h>
#include <stdlib.h>
#ifndef PLM_NO_STDIO
#include <stdio.h>
#endif

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef PLM_MALLOC
	#define PLM_MALLOC(sz) malloc(sz)
	#define PLM_FREE(p) free(p)
	#define PLM_REALLOC(p, sz) realloc(p, sz)
#endif

#define PLM_UNUSED(expr) (void)(expr)
#ifdef _MSC_VER
	#pragma warning(disable:4996)
#endif

// -----------------------------------------------------------------------------
// plm_buffer implementation

enum plm_buffer_mode {
	PLM_BUFFER_MODE_FILE,
	PLM_BUFFER_MODE_FIXED_MEM,
	PLM_BUFFER_MODE_RING,
	PLM_BUFFER_MODE_APPEND
};

struct plm_buffer_t {
	size_t bit_index;
	size_t capacity;
	size_t length;
	size_t total_size;
	int discard_read_bytes;
	int has_ended;
	int free_when_done;
#ifndef PLM_NO_STDIO
	int close_when_done;
	FILE *fh;
#endif
	plm_buffer_load_callback load_callback;
	plm_buffer_seek_callback seek_callback;
	plm_buffer_tell_callback tell_callback;
	void *load_callback_user_data;
	uint8_t *bytes;
	enum plm_buffer_mode mode;
};

typedef struct {
	int16_t index;
	int16_t value;
} plm_vlc_t;

typedef struct {
	int16_t index;
	uint16_t value;
} plm_vlc_uint_t;


void plm_buffer_seek(plm_buffer_t *self, size_t pos);
size_t plm_buffer_tell(plm_buffer_t *self);
void plm_buffer_discard_read_bytes(plm_buffer_t *self);

#ifndef PLM_NO_STDIO
void plm_buffer_load_file_callback(plm_buffer_t *self, void *user);
void plm_buffer_seek_file_callback(plm_buffer_t *self, size_t offset, void *user);
size_t plm_buffer_tell_file_callback(plm_buffer_t *self, void *user);
#endif

int plm_buffer_has(plm_buffer_t *self, size_t count);
int plm_buffer_read_slow(plm_buffer_t *self, int count);

// AmigaMCE: bits read a longword at a time where the buffer holds 4 more
// bytes (unaligned, as the 68020 and later read them); else, near its end,
// as pl_mpeg read them (plm_buffer_read_slow): the same bits either way.
#if defined(__GNUC__)
typedef uint32_t plm_u32_alias __attribute__((may_alias));
#else
typedef uint32_t plm_u32_alias;
#endif
#if defined(__m68k__) || defined(__mc68000__)
#define PLM_LD32(p) (*(const plm_u32_alias *)(const void *)(p))
#define PLM_ST32(p, v) (*(plm_u32_alias *)(void *)(p) = (v))
#else
static inline uint32_t PLM_LD32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
#define PLM_ST32(p, v) do { uint32_t v_ = (v); memcpy((p), &v_, 4); } while (0)
#endif
#if defined(__m68k__) || defined(__mc68000__)
#define PLM_BE32(p) (*(const plm_u32_alias *)(const void *)(p))
#else
#define PLM_BE32(p) (((uint32_t)(p)[0] << 24) | ((uint32_t)(p)[1] << 16) | \
	((uint32_t)(p)[2] << 8) | (uint32_t)(p)[3])
#endif

static inline int plm_buffer_read(plm_buffer_t *self, int count) {
	size_t i = self->bit_index >> 3;
	if (self->length - i >= 4 && count > 0 && count <= 25) {
		uint32_t w = PLM_BE32(self->bytes + i) << (self->bit_index & 7);
		self->bit_index += count;
		return (int)(w >> (32 - count));
	}
	return plm_buffer_read_slow(self, count);
}
void plm_buffer_align(plm_buffer_t *self);
void plm_buffer_skip(plm_buffer_t *self, size_t count);
int plm_buffer_skip_bytes(plm_buffer_t *self, uint8_t v);
int plm_buffer_next_start_code(plm_buffer_t *self);
int plm_buffer_find_start_code(plm_buffer_t *self, int code);
int plm_buffer_no_start_code(plm_buffer_t *self);
int16_t plm_buffer_read_vlc(plm_buffer_t *self, const plm_vlc_t *table);
uint16_t plm_buffer_read_vlc_uint(plm_buffer_t *self, const plm_vlc_uint_t *table);

#ifndef PLM_NO_STDIO

plm_buffer_t *plm_buffer_create_with_filename(const char *filename) {
	FILE *fh = fopen(filename, "rb");
	if (!fh) {
		return NULL;
	}
	return plm_buffer_create_with_file(fh, TRUE);
}

plm_buffer_t *plm_buffer_create_with_file(FILE *fh, int close_when_done) {
	plm_buffer_t *self = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
	self->fh = fh;
	self->close_when_done = close_when_done;
	self->mode = PLM_BUFFER_MODE_FILE;
	self->discard_read_bytes = TRUE;
	
	fseek(self->fh, 0, SEEK_END);
	self->total_size = ftell(self->fh);
	fseek(self->fh, 0, SEEK_SET);

	self->load_callback = plm_buffer_load_file_callback;
	self->seek_callback = plm_buffer_seek_file_callback;
	self->tell_callback = plm_buffer_tell_file_callback;
	return self;
}

#endif // PLM_NO_STDIO

plm_buffer_t *plm_buffer_create_with_callbacks(
	plm_buffer_load_callback load_callback,
	plm_buffer_seek_callback seek_callback,
	plm_buffer_tell_callback tell_callback,
	size_t length,
	void *user
) {
	plm_buffer_t *self = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
	self->mode = PLM_BUFFER_MODE_FILE;
	self->total_size = length;
	self->load_callback = load_callback;
	self->seek_callback = seek_callback;
	self->tell_callback = tell_callback;
	self->load_callback_user_data = user;
	return self;
}

plm_buffer_t *plm_buffer_create_with_memory(uint8_t *bytes, size_t length, int free_when_done) {
	plm_buffer_t *self = (plm_buffer_t *)PLM_MALLOC(sizeof(plm_buffer_t));
	memset(self, 0, sizeof(plm_buffer_t));
	self->capacity = length;
	self->length = length;
	self->total_size = length;
	self->free_when_done = free_when_done;
	self->bytes = bytes;
	self->mode = PLM_BUFFER_MODE_FIXED_MEM;
	self->discard_read_bytes = FALSE;
	return self;
}

plm_buffer_t *plm_buffer_create_with_capacity(size_t capacity) {
	plm_buffer_t *self = (plm_buffer_t *)PLM_MALLOC(sizeof(plm_buffer_t));
	memset(self, 0, sizeof(plm_buffer_t));
	self->capacity = capacity;
	self->free_when_done = TRUE;
	self->bytes = (uint8_t *)PLM_MALLOC(capacity);
	self->mode = PLM_BUFFER_MODE_RING;
	self->discard_read_bytes = TRUE;
	return self;
}

plm_buffer_t *plm_buffer_create_for_appending(size_t initial_capacity) {
	plm_buffer_t *self = plm_buffer_create_with_capacity(initial_capacity);
	self->mode = PLM_BUFFER_MODE_APPEND;
	self->discard_read_bytes = FALSE;
	return self;
}

void plm_buffer_destroy(plm_buffer_t *self) {
#ifndef PLM_NO_STDIO
	if (self->fh && self->close_when_done) {
		fclose(self->fh);
	}
#endif
	if (self->free_when_done) {
		PLM_FREE(self->bytes);
	}
	PLM_FREE(self);
}

size_t plm_buffer_get_size(plm_buffer_t *self) {
	return (self->mode == PLM_BUFFER_MODE_FILE)
		? self->total_size
		: self->length;
}

size_t plm_buffer_get_remaining(plm_buffer_t *self) {
	return self->length - (self->bit_index >> 3);
}

size_t plm_buffer_write(plm_buffer_t *self, uint8_t *bytes, size_t length) {
	if (self->mode == PLM_BUFFER_MODE_FIXED_MEM) {
		return 0;
	}

	if (self->discard_read_bytes) {
		// This should be a ring buffer, but instead it just shifts all unread 
		// data to the beginning of the buffer and appends new data at the end. 
		// Seems to be good enough.

		plm_buffer_discard_read_bytes(self);
		if (self->mode == PLM_BUFFER_MODE_RING) {
			self->total_size = 0;
		}
	}

	// Do we have to resize to fit the new data?
	size_t bytes_available = self->capacity - self->length;
	if (bytes_available < length) {
		size_t new_size = self->capacity;
		do {
			new_size *= 2;
		} while (new_size - self->length < length);
		self->bytes = (uint8_t *)PLM_REALLOC(self->bytes, new_size);
		self->capacity = new_size;
	}

	memcpy(self->bytes + self->length, bytes, length);
	self->length += length;
	self->has_ended = FALSE;
	return length;
}

void plm_buffer_signal_end(plm_buffer_t *self) {
	self->total_size = self->length;
}

void plm_buffer_set_load_callback(plm_buffer_t *self, plm_buffer_load_callback fp, void *user) {
	self->load_callback = fp;
	self->load_callback_user_data = user;
}

void plm_buffer_rewind(plm_buffer_t *self) {
	plm_buffer_seek(self, 0);
}

void plm_buffer_seek(plm_buffer_t *self, size_t pos) {
	self->has_ended = FALSE;

	if (self->seek_callback) {
		self->seek_callback(self, pos, self->load_callback_user_data);
		self->bit_index = 0;
		self->length = 0;
	}
	else if (self->mode == PLM_BUFFER_MODE_RING) {
		if (pos != 0) {
			// Seeking to non-0 is forbidden for dynamic-mem buffers
			return; 
		}
		self->bit_index = 0;
		self->length = 0;
		self->total_size = 0;
	}
	else if (pos < self->length) {
		self->bit_index = pos << 3;
	}
}

size_t plm_buffer_tell(plm_buffer_t *self) {
	return self->tell_callback
		? self->tell_callback(self, self->load_callback_user_data) + (self->bit_index >> 3) - self->length
		: self->bit_index >> 3;
}

void plm_buffer_discard_read_bytes(plm_buffer_t *self) {
	size_t byte_pos = self->bit_index >> 3;
	if (byte_pos == self->length) {
		self->bit_index = 0;
		self->length = 0;
	}
	else if (byte_pos > 0) {
		memmove(self->bytes, self->bytes + byte_pos, self->length - byte_pos);
		self->bit_index -= byte_pos << 3;
		self->length -= byte_pos;
	}
}

#ifndef PLM_NO_STDIO

void plm_buffer_load_file_callback(plm_buffer_t *self, void *user) {
	PLM_UNUSED(user);
	
	if (self->discard_read_bytes) {
		plm_buffer_discard_read_bytes(self);
	}

	size_t bytes_available = self->capacity - self->length;
	size_t bytes_read = fread(self->bytes + self->length, 1, bytes_available, self->fh);
	self->length += bytes_read;

	if (bytes_read == 0) {
		self->has_ended = TRUE;
	}
}

void plm_buffer_seek_file_callback(plm_buffer_t *self, size_t offset, void *user) {
	PLM_UNUSED(user);
	fseek(self->fh, offset, SEEK_SET);
}

size_t plm_buffer_tell_file_callback(plm_buffer_t *self, void *user) {
	PLM_UNUSED(user);
	return ftell(self->fh);
}

#endif // PLM_NO_STDIO

int plm_buffer_has_ended(plm_buffer_t *self) {
	return self->has_ended;
}

int plm_buffer_has(plm_buffer_t *self, size_t count) {
	if (((self->length << 3) - self->bit_index) >= count) {
		return TRUE;
	}

	if (self->load_callback) {
		self->load_callback(self, self->load_callback_user_data);
		
		if (((self->length << 3) - self->bit_index) >= count) {
			return TRUE;
		}
	}	
	
	if (self->total_size != 0 && self->length == self->total_size) {
		self->has_ended = TRUE;
	}
	return FALSE;
}

int plm_buffer_read_slow(plm_buffer_t *self, int count) {
	if (!plm_buffer_has(self, count)) {
		return 0;
	}

	int value = 0;
	while (count) {
		int current_byte = self->bytes[self->bit_index >> 3];

		int remaining = 8 - (self->bit_index & 7); // Remaining bits in byte
		int read = remaining < count ? remaining : count; // Bits in self run
		int shift = remaining - read;
		int mask = (0xff >> (8 - read));

		value = (value << read) | ((current_byte & (mask << shift)) >> shift);

		self->bit_index += read;
		count -= read;
	}

	return value;
}

void plm_buffer_align(plm_buffer_t *self) {
	self->bit_index = ((self->bit_index + 7) >> 3) << 3; // Align to next byte
}

void plm_buffer_skip(plm_buffer_t *self, size_t count) {
	if (plm_buffer_has(self, count)) {
		self->bit_index += count;
	}
}

int plm_buffer_skip_bytes(plm_buffer_t *self, uint8_t v) {
	plm_buffer_align(self);
	int skipped = 0;
	while (plm_buffer_has(self, 8) && self->bytes[self->bit_index >> 3] == v) {
		self->bit_index += 8;
		skipped++;
	}
	return skipped;
}

int plm_buffer_next_start_code(plm_buffer_t *self) {
	plm_buffer_align(self);

	while (plm_buffer_has(self, (5 << 3))) {
		// AmigaMCE: through what the buffer holds at once (5 bytes at
		// least ahead, as above), only then asking it for more
		size_t byte_index = (self->bit_index) >> 3;
		const uint8_t *b = self->bytes;
		size_t end = self->length - 5;
		for (;;) {
			if (b[byte_index + 2] > 1) {
				// (not 00 00 01 here, nor with its first byte one or two on)
				if (byte_index + 3 > end) {
					break;
				}
				byte_index += 3;
				continue;
			}
			if (b[byte_index] == 0x00 && b[byte_index + 1] == 0x00 && b[byte_index + 2] == 0x01) {
				self->bit_index = (byte_index + 4) << 3;
				return b[byte_index + 3];
			}
			if (byte_index + 1 > end) {
				break;
			}
			byte_index++;
		}
		self->bit_index = (byte_index + 1) << 3;
	}
	return -1;
}

int plm_buffer_find_start_code(plm_buffer_t *self, int code) {
	int current = 0;
	while (TRUE) {
		current = plm_buffer_next_start_code(self);
		if (current == code || current == -1) {
			return current;
		}
	}
	return -1;
}

int plm_buffer_has_start_code(plm_buffer_t *self, int code) {
	size_t previous_bit_index = self->bit_index;
	int previous_discard_read_bytes = self->discard_read_bytes;
	
	self->discard_read_bytes = FALSE;
	int current = plm_buffer_find_start_code(self, code);

	self->bit_index = previous_bit_index;
	self->discard_read_bytes = previous_discard_read_bytes;
	return current;
}

int plm_buffer_peek_non_zero(plm_buffer_t *self, int bit_count) {
	if (!plm_buffer_has(self, bit_count)) {
		return FALSE;
	}

	int val = plm_buffer_read(self, bit_count);
	self->bit_index -= bit_count;
	return val != 0;
}

int16_t plm_buffer_read_vlc(plm_buffer_t *self, const plm_vlc_t *table) {
	plm_vlc_t state = {0, 0};
	do {
		state = table[state.index + plm_buffer_read(self, 1)];
	} while (state.index > 0);
	return state.value;
}

uint16_t plm_buffer_read_vlc_uint(plm_buffer_t *self, const plm_vlc_uint_t *table) {
	return (uint16_t)plm_buffer_read_vlc(self, (const plm_vlc_t *)table);
}

// AmigaMCE: a VLC by a table of its first bits (plm_vlc_fast_build: each
// pattern of them the code it begins and its length, or the tree's node
// it goes on from), where the buffer holds 4 more bytes; else bit by bit
// as above. The same codes either way: the tables are made by walking the
// same trees.
typedef struct {
	int16_t value;   // the code's value; or, length 0, the node to go on from
	uint8_t length;  // its bits (0: longer than the table's)
	uint8_t pad;
} plm_vlc_fast_t;

static void plm_vlc_fast_build(const plm_vlc_t *table, plm_vlc_fast_t *fast, int bits) {
	for (int pattern = 0; pattern < (1 << bits); pattern++) {
		int index = 0;
		plm_vlc_fast_t e = {0, 0, 0};
		for (int b = 0; b < bits; b++) {
			plm_vlc_t state = table[index + ((pattern >> (bits - 1 - b)) & 1)];
			if (state.index <= 0) {
				e.value = state.value;
				e.length = (uint8_t)(b + 1);
				break;
			}
			index = state.index;
		}
		if (!e.length) {
			e.value = (int16_t)index;
		}
		fast[pattern] = e;
	}
}

static inline int16_t plm_buffer_read_vlc_fast(
	plm_buffer_t *self, const plm_vlc_t *table, const plm_vlc_fast_t *fast, int bits
) {
	size_t i = self->bit_index >> 3;
	if (self->length - i >= 4) {
		uint32_t w = PLM_BE32(self->bytes + i) << (self->bit_index & 7);
		const plm_vlc_fast_t *e = &fast[w >> (32 - bits)];
		if (e->length) {
			self->bit_index += e->length;
			return e->value;
		}
		// longer: on through the tree from where the table left it (25
		// bits are in w; no code here is that long)
		plm_vlc_t state;
		int index = e->value;
		int b = bits;
		do {
			state = table[index + ((w >> (31 - b)) & 1)];
			index = state.index;
			b++;
		} while (state.index > 0 && b < 25);
		self->bit_index += b;
		while (state.index > 0) {
			state = table[state.index + plm_buffer_read(self, 1)];
		}
		return state.value;
	}
	return plm_buffer_read_vlc(self, table);
}



// ----------------------------------------------------------------------------
// plm_demux implementation

static const int PLM_START_PACK = 0xBA;
static const int PLM_START_END = 0xB9;
static const int PLM_START_SYSTEM = 0xBB;

struct plm_demux_t {
	plm_buffer_t *buffer;
	int destroy_buffer_when_done;

	size_t last_file_size;
	int64_t last_decoded_clock;	// mpegv: the same, as stored (90 kHz)

	int start_code;
	int has_pack_header;
	int has_system_header;
	int has_headers;

	int num_audio_streams;
	int num_video_streams;
	plm_packet_t current_packet;
	plm_packet_t next_packet;
};


void plm_demux_buffer_seek(plm_demux_t *self, size_t pos);
int64_t plm_demux_decode_time(plm_demux_t *self);
plm_packet_t *plm_demux_decode_packet(plm_demux_t *self, int type);
plm_packet_t *plm_demux_get_packet(plm_demux_t *self);

plm_demux_t *plm_demux_create(plm_buffer_t *buffer, int destroy_when_done) {
	plm_demux_t *self = (plm_demux_t *)PLM_MALLOC(sizeof(plm_demux_t));
	memset(self, 0, sizeof(plm_demux_t));

	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;

	self->start_code = -1;

	plm_demux_has_headers(self);
	return self;
}

void plm_demux_destroy(plm_demux_t *self) {
	if (self->destroy_buffer_when_done) {
		plm_buffer_destroy(self->buffer);
	}
	PLM_FREE(self);
}

int plm_demux_has_headers(plm_demux_t *self) {
	if (self->has_headers) {
		return TRUE;
	}

	// Decode pack header
	if (!self->has_pack_header) {
		if (
			self->start_code != PLM_START_PACK &&
			plm_buffer_find_start_code(self->buffer, PLM_START_PACK) == -1
		) {
			return FALSE;
		}

		self->start_code = PLM_START_PACK;
		if (!plm_buffer_has(self->buffer, 64)) {
			return FALSE;
		}
		self->start_code = -1;

		if (plm_buffer_read(self->buffer, 4) != 0x02) {
			return FALSE;
		}

		plm_demux_decode_time(self);	// mpegv: the system clock, not kept
		plm_buffer_skip(self->buffer, 1);
		plm_buffer_skip(self->buffer, 22); // mux_rate * 50
		plm_buffer_skip(self->buffer, 1);

		self->has_pack_header = TRUE;
	}

	// Decode system header
	if (!self->has_system_header) {
		if (
			self->start_code != PLM_START_SYSTEM &&
			plm_buffer_find_start_code(self->buffer, PLM_START_SYSTEM) == -1
		) {
			return FALSE;
		}

		self->start_code = PLM_START_SYSTEM;
		if (!plm_buffer_has(self->buffer, 56)) {
			return FALSE;
		}
		self->start_code = -1;

		plm_buffer_skip(self->buffer, 16); // header_length
		plm_buffer_skip(self->buffer, 24); // rate bound
		self->num_audio_streams = plm_buffer_read(self->buffer, 6);
		plm_buffer_skip(self->buffer, 5); // misc flags
		self->num_video_streams = plm_buffer_read(self->buffer, 5);

		self->has_system_header = TRUE;
	}

	self->has_headers = TRUE;
	return TRUE;
}

int plm_demux_probe(plm_demux_t *self, size_t probesize) {
	int previous_pos = plm_buffer_tell(self->buffer);

	int video_stream = FALSE;
	int audio_streams[4] = {FALSE, FALSE, FALSE, FALSE};
	do {
		self->start_code = plm_buffer_next_start_code(self->buffer);
		if (self->start_code == PLM_DEMUX_PACKET_VIDEO_1) {
			video_stream = TRUE;
		}
		else if (
			self->start_code >= PLM_DEMUX_PACKET_AUDIO_1 && 
			self->start_code <= PLM_DEMUX_PACKET_AUDIO_4
		) {
			audio_streams[self->start_code - PLM_DEMUX_PACKET_AUDIO_1] = TRUE;
		}
	} while (
		self->start_code != -1 && 
		plm_buffer_tell(self->buffer) - previous_pos < probesize
	);

	self->num_video_streams = video_stream ? 1 : 0;
	self->num_audio_streams = 0;
	for (int i = 0; i < 4; i++) {
		if (audio_streams[i]) {
			self->num_audio_streams++;
		}
	}

	plm_demux_buffer_seek(self, previous_pos);
	return (self->num_video_streams || self->num_audio_streams);
}

int plm_demux_get_num_video_streams(plm_demux_t *self) {
	return plm_demux_has_headers(self)
		? self->num_video_streams
		: 0;
}

int plm_demux_get_num_audio_streams(plm_demux_t *self) {
	return plm_demux_has_headers(self)
		? self->num_audio_streams
		: 0;
}

void plm_demux_rewind(plm_demux_t *self) {
	plm_buffer_rewind(self->buffer);
	self->current_packet.length = 0;
	self->next_packet.length = 0;
	self->start_code = -1;
}

int plm_demux_has_ended(plm_demux_t *self) {
	return plm_buffer_has_ended(self->buffer);
}

void plm_demux_buffer_seek(plm_demux_t *self, size_t pos) {
	plm_buffer_seek(self->buffer, pos);
	self->current_packet.length = 0;
	self->next_packet.length = 0;
	self->start_code = -1;
}

plm_packet_t *plm_demux_decode(plm_demux_t *self) {
	if (!plm_demux_has_headers(self)) {
		return NULL;
	}

	if (self->current_packet.length) {
		size_t bits_till_next_packet = self->current_packet.length << 3;
		if (!plm_buffer_has(self->buffer, bits_till_next_packet)) {
			return NULL;
		}
		plm_buffer_skip(self->buffer, bits_till_next_packet);
		self->current_packet.length = 0;
	}

	// Pending packet waiting for data?
	if (self->next_packet.length) {
		return plm_demux_get_packet(self);
	}

	// Pending packet waiting for header?
	if (self->start_code != -1) {
		return plm_demux_decode_packet(self, self->start_code);
	}

	do {
		self->start_code = plm_buffer_next_start_code(self->buffer);
		if (
			self->start_code == PLM_DEMUX_PACKET_VIDEO_1 || 
			self->start_code == PLM_DEMUX_PACKET_PRIVATE || (
				self->start_code >= PLM_DEMUX_PACKET_AUDIO_1 && 
				self->start_code <= PLM_DEMUX_PACKET_AUDIO_4
			)
		) {
			return plm_demux_decode_packet(self, self->start_code);
		}
	} while (self->start_code != -1);

	return NULL;
}

int64_t plm_demux_decode_time(plm_demux_t *self) {
	int64_t clock = plm_buffer_read(self->buffer, 3) << 30;
	plm_buffer_skip(self->buffer, 1);
	clock |= plm_buffer_read(self->buffer, 15) << 15;
	plm_buffer_skip(self->buffer, 1);
	clock |= plm_buffer_read(self->buffer, 15);
	plm_buffer_skip(self->buffer, 1);
	self->last_decoded_clock = clock;	// mpegv
	return clock;	// mpegv: 90 kHz, not seconds in floating point
}

plm_packet_t *plm_demux_decode_packet(plm_demux_t *self, int type) {
	if (!plm_buffer_has(self->buffer, 16 << 3)) {
		return NULL;
	}

	self->start_code = -1;

	self->next_packet.type = type;
	self->next_packet.length = plm_buffer_read(self->buffer, 16);
	self->next_packet.length -= plm_buffer_skip_bytes(self->buffer, 0xff); // stuffing

	// skip P-STD
	if (plm_buffer_read(self->buffer, 2) == 0x01) {
		plm_buffer_skip(self->buffer, 16);
		self->next_packet.length -= 2;
	}

	int pts_dts_marker = plm_buffer_read(self->buffer, 2);
	self->next_packet.pts90 = -1;	// mpegv
	if (pts_dts_marker == 0x03) {
		self->next_packet.pts90 = plm_demux_decode_time(self);	// mpegv
		plm_buffer_skip(self->buffer, 40); // skip dts
		self->next_packet.length -= 10;
	}
	else if (pts_dts_marker == 0x02) {
		self->next_packet.pts90 = plm_demux_decode_time(self);	// mpegv
		self->next_packet.length -= 5;
	}
	else if (pts_dts_marker == 0x00) {
		plm_buffer_skip(self->buffer, 4);
		self->next_packet.length -= 1;
	}
	else {
		return NULL; // invalid
	}
	
	return plm_demux_get_packet(self);
}

plm_packet_t *plm_demux_get_packet(plm_demux_t *self) {
	if (!plm_buffer_has(self->buffer, self->next_packet.length << 3)) {
		return NULL;
	}

	self->current_packet.data = self->buffer->bytes + (self->buffer->bit_index >> 3);
	self->current_packet.length = self->next_packet.length;
	self->current_packet.type = self->next_packet.type;
	self->current_packet.pts90 = self->next_packet.pts90;	// mpegv

	self->next_packet.length = 0;
	return &self->current_packet;
}



// -----------------------------------------------------------------------------
// plm_video implementation

// Inspired by Java MPEG-1 Video Decoder and Player by Zoltan Korandi 
// https://sourceforge.net/projects/javampeg1video/

static const int PLM_VIDEO_PICTURE_TYPE_INTRA = 1;
static const int PLM_VIDEO_PICTURE_TYPE_PREDICTIVE = 2;
static const int PLM_VIDEO_PICTURE_TYPE_B = 3;

static const int PLM_START_SEQUENCE = 0xB3;
static const int PLM_START_SLICE_FIRST = 0x01;
static const int PLM_START_SLICE_LAST = 0xAF;
static const int PLM_START_PICTURE = 0x00;
static const int PLM_START_EXTENSION = 0xB5;
static const int PLM_START_USER_DATA = 0xB2;

#define PLM_START_IS_SLICE(c) \
	(c >= PLM_START_SLICE_FIRST && c <= PLM_START_SLICE_LAST)

static const uint8_t PLM_VIDEO_ZIG_ZAG[] = {
	 0,  1,  8, 16,  9,  2,  3, 10,
	17, 24, 32, 25, 18, 11,  4,  5,
	12, 19, 26, 33, 40, 48, 41, 34,
	27, 20, 13,  6,  7, 14, 21, 28,
	35, 42, 49, 56, 57, 50, 43, 36,
	29, 22, 15, 23, 30, 37, 44, 51,
	58, 59, 52, 45, 38, 31, 39, 46,
	53, 60, 61, 54, 47, 55, 62, 63
};

static const uint8_t PLM_VIDEO_INTRA_QUANT_MATRIX[] = {
	 8, 16, 19, 22, 26, 27, 29, 34,
	16, 16, 22, 24, 27, 29, 34, 37,
	19, 22, 26, 27, 29, 34, 34, 38,
	22, 22, 26, 27, 29, 34, 37, 40,
	22, 26, 27, 29, 32, 35, 40, 48,
	26, 27, 29, 32, 35, 40, 48, 58,
	26, 27, 29, 34, 38, 46, 56, 69,
	27, 29, 35, 38, 46, 56, 69, 83
};

static const uint8_t PLM_VIDEO_NON_INTRA_QUANT_MATRIX[] = {
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16,
	16, 16, 16, 16, 16, 16, 16, 16
};

static const uint8_t PLM_VIDEO_PREMULTIPLIER_MATRIX[] = {
	32, 44, 42, 38, 32, 25, 17,  9,
	44, 62, 58, 52, 44, 35, 24, 12,
	42, 58, 55, 49, 42, 33, 23, 12,
	38, 52, 49, 44, 38, 30, 20, 10,
	32, 44, 42, 38, 32, 25, 17,  9,
	25, 35, 33, 30, 25, 20, 14,  7,
	17, 24, 23, 20, 17, 14,  9,  5,
	 9, 12, 12, 10,  9,  7,  5,  2
};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT[] = {
	{  1 << 1,    0}, {       0,    1},  //   0: x
	{  2 << 1,    0}, {  3 << 1,    0},  //   1: 0x
	{  4 << 1,    0}, {  5 << 1,    0},  //   2: 00x
	{       0,    3}, {       0,    2},  //   3: 01x
	{  6 << 1,    0}, {  7 << 1,    0},  //   4: 000x
	{       0,    5}, {       0,    4},  //   5: 001x
	{  8 << 1,    0}, {  9 << 1,    0},  //   6: 0000x
	{       0,    7}, {       0,    6},  //   7: 0001x
	{ 10 << 1,    0}, { 11 << 1,    0},  //   8: 0000 0x
	{ 12 << 1,    0}, { 13 << 1,    0},  //   9: 0000 1x
	{ 14 << 1,    0}, { 15 << 1,    0},  //  10: 0000 00x
	{ 16 << 1,    0}, { 17 << 1,    0},  //  11: 0000 01x
	{ 18 << 1,    0}, { 19 << 1,    0},  //  12: 0000 10x
	{       0,    9}, {       0,    8},  //  13: 0000 11x
	{      -1,    0}, { 20 << 1,    0},  //  14: 0000 000x
	{      -1,    0}, { 21 << 1,    0},  //  15: 0000 001x
	{ 22 << 1,    0}, { 23 << 1,    0},  //  16: 0000 010x
	{       0,   15}, {       0,   14},  //  17: 0000 011x
	{       0,   13}, {       0,   12},  //  18: 0000 100x
	{       0,   11}, {       0,   10},  //  19: 0000 101x
	{ 24 << 1,    0}, { 25 << 1,    0},  //  20: 0000 0001x
	{ 26 << 1,    0}, { 27 << 1,    0},  //  21: 0000 0011x
	{ 28 << 1,    0}, { 29 << 1,    0},  //  22: 0000 0100x
	{ 30 << 1,    0}, { 31 << 1,    0},  //  23: 0000 0101x
	{ 32 << 1,    0}, {      -1,    0},  //  24: 0000 0001 0x
	{      -1,    0}, { 33 << 1,    0},  //  25: 0000 0001 1x
	{ 34 << 1,    0}, { 35 << 1,    0},  //  26: 0000 0011 0x
	{ 36 << 1,    0}, { 37 << 1,    0},  //  27: 0000 0011 1x
	{ 38 << 1,    0}, { 39 << 1,    0},  //  28: 0000 0100 0x
	{       0,   21}, {       0,   20},  //  29: 0000 0100 1x
	{       0,   19}, {       0,   18},  //  30: 0000 0101 0x
	{       0,   17}, {       0,   16},  //  31: 0000 0101 1x
	{       0,   35}, {      -1,    0},  //  32: 0000 0001 00x
	{      -1,    0}, {       0,   34},  //  33: 0000 0001 11x
	{       0,   33}, {       0,   32},  //  34: 0000 0011 00x
	{       0,   31}, {       0,   30},  //  35: 0000 0011 01x
	{       0,   29}, {       0,   28},  //  36: 0000 0011 10x
	{       0,   27}, {       0,   26},  //  37: 0000 0011 11x
	{       0,   25}, {       0,   24},  //  38: 0000 0100 00x
	{       0,   23}, {       0,   22},  //  39: 0000 0100 01x
};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_INTRA[] = {
	{  1 << 1,    0}, {       0,  0x01},  //   0: x
	{      -1,    0}, {       0,  0x11},  //   1: 0x
};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_PREDICTIVE[] = {
	{  1 << 1,    0}, {       0, 0x0a},  //   0: x
	{  2 << 1,    0}, {       0, 0x02},  //   1: 0x
	{  3 << 1,    0}, {       0, 0x08},  //   2: 00x
	{  4 << 1,    0}, {  5 << 1,    0},  //   3: 000x
	{  6 << 1,    0}, {       0, 0x12},  //   4: 0000x
	{       0, 0x1a}, {       0, 0x01},  //   5: 0001x
	{      -1,    0}, {       0, 0x11},  //   6: 0000 0x
};

static const plm_vlc_t PLM_VIDEO_MACROBLOCK_TYPE_B[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{  3 << 1,    0}, {  4 << 1,    0},  //   1: 0x
	{       0, 0x0c}, {       0, 0x0e},  //   2: 1x
	{  5 << 1,    0}, {  6 << 1,    0},  //   3: 00x
	{       0, 0x04}, {       0, 0x06},  //   4: 01x
	{  7 << 1,    0}, {  8 << 1,    0},  //   5: 000x
	{       0, 0x08}, {       0, 0x0a},  //   6: 001x
	{  9 << 1,    0}, { 10 << 1,    0},  //   7: 0000x
	{       0, 0x1e}, {       0, 0x01},  //   8: 0001x
	{      -1,    0}, {       0, 0x11},  //   9: 0000 0x
	{       0, 0x16}, {       0, 0x1a},  //  10: 0000 1x
};

static const plm_vlc_t *PLM_VIDEO_MACROBLOCK_TYPE[] = {
	NULL,
	PLM_VIDEO_MACROBLOCK_TYPE_INTRA,
	PLM_VIDEO_MACROBLOCK_TYPE_PREDICTIVE,
	PLM_VIDEO_MACROBLOCK_TYPE_B
};

static const plm_vlc_t PLM_VIDEO_CODE_BLOCK_PATTERN[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{  3 << 1,    0}, {  4 << 1,    0},  //   1: 0x
	{  5 << 1,    0}, {  6 << 1,    0},  //   2: 1x
	{  7 << 1,    0}, {  8 << 1,    0},  //   3: 00x
	{  9 << 1,    0}, { 10 << 1,    0},  //   4: 01x
	{ 11 << 1,    0}, { 12 << 1,    0},  //   5: 10x
	{ 13 << 1,    0}, {       0,   60},  //   6: 11x
	{ 14 << 1,    0}, { 15 << 1,    0},  //   7: 000x
	{ 16 << 1,    0}, { 17 << 1,    0},  //   8: 001x
	{ 18 << 1,    0}, { 19 << 1,    0},  //   9: 010x
	{ 20 << 1,    0}, { 21 << 1,    0},  //  10: 011x
	{ 22 << 1,    0}, { 23 << 1,    0},  //  11: 100x
	{       0,   32}, {       0,   16},  //  12: 101x
	{       0,    8}, {       0,    4},  //  13: 110x
	{ 24 << 1,    0}, { 25 << 1,    0},  //  14: 0000x
	{ 26 << 1,    0}, { 27 << 1,    0},  //  15: 0001x
	{ 28 << 1,    0}, { 29 << 1,    0},  //  16: 0010x
	{ 30 << 1,    0}, { 31 << 1,    0},  //  17: 0011x
	{       0,   62}, {       0,    2},  //  18: 0100x
	{       0,   61}, {       0,    1},  //  19: 0101x
	{       0,   56}, {       0,   52},  //  20: 0110x
	{       0,   44}, {       0,   28},  //  21: 0111x
	{       0,   40}, {       0,   20},  //  22: 1000x
	{       0,   48}, {       0,   12},  //  23: 1001x
	{ 32 << 1,    0}, { 33 << 1,    0},  //  24: 0000 0x
	{ 34 << 1,    0}, { 35 << 1,    0},  //  25: 0000 1x
	{ 36 << 1,    0}, { 37 << 1,    0},  //  26: 0001 0x
	{ 38 << 1,    0}, { 39 << 1,    0},  //  27: 0001 1x
	{ 40 << 1,    0}, { 41 << 1,    0},  //  28: 0010 0x
	{ 42 << 1,    0}, { 43 << 1,    0},  //  29: 0010 1x
	{       0,   63}, {       0,    3},  //  30: 0011 0x
	{       0,   36}, {       0,   24},  //  31: 0011 1x
	{ 44 << 1,    0}, { 45 << 1,    0},  //  32: 0000 00x
	{ 46 << 1,    0}, { 47 << 1,    0},  //  33: 0000 01x
	{ 48 << 1,    0}, { 49 << 1,    0},  //  34: 0000 10x
	{ 50 << 1,    0}, { 51 << 1,    0},  //  35: 0000 11x
	{ 52 << 1,    0}, { 53 << 1,    0},  //  36: 0001 00x
	{ 54 << 1,    0}, { 55 << 1,    0},  //  37: 0001 01x
	{ 56 << 1,    0}, { 57 << 1,    0},  //  38: 0001 10x
	{ 58 << 1,    0}, { 59 << 1,    0},  //  39: 0001 11x
	{       0,   34}, {       0,   18},  //  40: 0010 00x
	{       0,   10}, {       0,    6},  //  41: 0010 01x
	{       0,   33}, {       0,   17},  //  42: 0010 10x
	{       0,    9}, {       0,    5},  //  43: 0010 11x
	{      -1,    0}, { 60 << 1,    0},  //  44: 0000 000x
	{ 61 << 1,    0}, { 62 << 1,    0},  //  45: 0000 001x
	{       0,   58}, {       0,   54},  //  46: 0000 010x
	{       0,   46}, {       0,   30},  //  47: 0000 011x
	{       0,   57}, {       0,   53},  //  48: 0000 100x
	{       0,   45}, {       0,   29},  //  49: 0000 101x
	{       0,   38}, {       0,   26},  //  50: 0000 110x
	{       0,   37}, {       0,   25},  //  51: 0000 111x
	{       0,   43}, {       0,   23},  //  52: 0001 000x
	{       0,   51}, {       0,   15},  //  53: 0001 001x
	{       0,   42}, {       0,   22},  //  54: 0001 010x
	{       0,   50}, {       0,   14},  //  55: 0001 011x
	{       0,   41}, {       0,   21},  //  56: 0001 100x
	{       0,   49}, {       0,   13},  //  57: 0001 101x
	{       0,   35}, {       0,   19},  //  58: 0001 110x
	{       0,   11}, {       0,    7},  //  59: 0001 111x
	{       0,   39}, {       0,   27},  //  60: 0000 0001x
	{       0,   59}, {       0,   55},  //  61: 0000 0010x
	{       0,   47}, {       0,   31},  //  62: 0000 0011x
};

static const plm_vlc_t PLM_VIDEO_MOTION[] = {
	{  1 << 1,    0}, {       0,    0},  //   0: x
	{  2 << 1,    0}, {  3 << 1,    0},  //   1: 0x
	{  4 << 1,    0}, {  5 << 1,    0},  //   2: 00x
	{       0,    1}, {       0,   -1},  //   3: 01x
	{  6 << 1,    0}, {  7 << 1,    0},  //   4: 000x
	{       0,    2}, {       0,   -2},  //   5: 001x
	{  8 << 1,    0}, {  9 << 1,    0},  //   6: 0000x
	{       0,    3}, {       0,   -3},  //   7: 0001x
	{ 10 << 1,    0}, { 11 << 1,    0},  //   8: 0000 0x
	{ 12 << 1,    0}, { 13 << 1,    0},  //   9: 0000 1x
	{      -1,    0}, { 14 << 1,    0},  //  10: 0000 00x
	{ 15 << 1,    0}, { 16 << 1,    0},  //  11: 0000 01x
	{ 17 << 1,    0}, { 18 << 1,    0},  //  12: 0000 10x
	{       0,    4}, {       0,   -4},  //  13: 0000 11x
	{      -1,    0}, { 19 << 1,    0},  //  14: 0000 001x
	{ 20 << 1,    0}, { 21 << 1,    0},  //  15: 0000 010x
	{       0,    7}, {       0,   -7},  //  16: 0000 011x
	{       0,    6}, {       0,   -6},  //  17: 0000 100x
	{       0,    5}, {       0,   -5},  //  18: 0000 101x
	{ 22 << 1,    0}, { 23 << 1,    0},  //  19: 0000 0011x
	{ 24 << 1,    0}, { 25 << 1,    0},  //  20: 0000 0100x
	{ 26 << 1,    0}, { 27 << 1,    0},  //  21: 0000 0101x
	{ 28 << 1,    0}, { 29 << 1,    0},  //  22: 0000 0011 0x
	{ 30 << 1,    0}, { 31 << 1,    0},  //  23: 0000 0011 1x
	{ 32 << 1,    0}, { 33 << 1,    0},  //  24: 0000 0100 0x
	{       0,   10}, {       0,  -10},  //  25: 0000 0100 1x
	{       0,    9}, {       0,   -9},  //  26: 0000 0101 0x
	{       0,    8}, {       0,   -8},  //  27: 0000 0101 1x
	{       0,   16}, {       0,  -16},  //  28: 0000 0011 00x
	{       0,   15}, {       0,  -15},  //  29: 0000 0011 01x
	{       0,   14}, {       0,  -14},  //  30: 0000 0011 10x
	{       0,   13}, {       0,  -13},  //  31: 0000 0011 11x
	{       0,   12}, {       0,  -12},  //  32: 0000 0100 00x
	{       0,   11}, {       0,  -11},  //  33: 0000 0100 01x
};

static const plm_vlc_t PLM_VIDEO_DCT_SIZE_LUMINANCE[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{       0,    1}, {       0,    2},  //   1: 0x
	{  3 << 1,    0}, {  4 << 1,    0},  //   2: 1x
	{       0,    0}, {       0,    3},  //   3: 10x
	{       0,    4}, {  5 << 1,    0},  //   4: 11x
	{       0,    5}, {  6 << 1,    0},  //   5: 111x
	{       0,    6}, {  7 << 1,    0},  //   6: 1111x
	{       0,    7}, {  8 << 1,    0},  //   7: 1111 1x
	{       0,    8}, {      -1,    0},  //   8: 1111 11x
};

static const plm_vlc_t PLM_VIDEO_DCT_SIZE_CHROMINANCE[] = {
	{  1 << 1,    0}, {  2 << 1,    0},  //   0: x
	{       0,    0}, {       0,    1},  //   1: 0x
	{       0,    2}, {  3 << 1,    0},  //   2: 1x
	{       0,    3}, {  4 << 1,    0},  //   3: 11x
	{       0,    4}, {  5 << 1,    0},  //   4: 111x
	{       0,    5}, {  6 << 1,    0},  //   5: 1111x
	{       0,    6}, {  7 << 1,    0},  //   6: 1111 1x
	{       0,    7}, {  8 << 1,    0},  //   7: 1111 11x
	{       0,    8}, {      -1,    0},  //   8: 1111 111x
};

static const plm_vlc_t *PLM_VIDEO_DCT_SIZE[] = {
	PLM_VIDEO_DCT_SIZE_LUMINANCE,
	PLM_VIDEO_DCT_SIZE_CHROMINANCE,
	PLM_VIDEO_DCT_SIZE_CHROMINANCE
};


//  dct_coeff bitmap:
//    0xff00  run
//    0x00ff  level

//  Decoded values are unsigned. Sign bit follows in the stream.

static const plm_vlc_uint_t PLM_VIDEO_DCT_COEFF[] = {
	{  1 << 1,        0}, {       0,   0x0001},  //   0: x
	{  2 << 1,        0}, {  3 << 1,        0},  //   1: 0x
	{  4 << 1,        0}, {  5 << 1,        0},  //   2: 00x
	{  6 << 1,        0}, {       0,   0x0101},  //   3: 01x
	{  7 << 1,        0}, {  8 << 1,        0},  //   4: 000x
	{  9 << 1,        0}, { 10 << 1,        0},  //   5: 001x
	{       0,   0x0002}, {       0,   0x0201},  //   6: 010x
	{ 11 << 1,        0}, { 12 << 1,        0},  //   7: 0000x
	{ 13 << 1,        0}, { 14 << 1,        0},  //   8: 0001x
	{ 15 << 1,        0}, {       0,   0x0003},  //   9: 0010x
	{       0,   0x0401}, {       0,   0x0301},  //  10: 0011x
	{ 16 << 1,        0}, {       0,   0xffff},  //  11: 0000 0x
	{ 17 << 1,        0}, { 18 << 1,        0},  //  12: 0000 1x
	{       0,   0x0701}, {       0,   0x0601},  //  13: 0001 0x
	{       0,   0x0102}, {       0,   0x0501},  //  14: 0001 1x
	{ 19 << 1,        0}, { 20 << 1,        0},  //  15: 0010 0x
	{ 21 << 1,        0}, { 22 << 1,        0},  //  16: 0000 00x
	{       0,   0x0202}, {       0,   0x0901},  //  17: 0000 10x
	{       0,   0x0004}, {       0,   0x0801},  //  18: 0000 11x
	{ 23 << 1,        0}, { 24 << 1,        0},  //  19: 0010 00x
	{ 25 << 1,        0}, { 26 << 1,        0},  //  20: 0010 01x
	{ 27 << 1,        0}, { 28 << 1,        0},  //  21: 0000 000x
	{ 29 << 1,        0}, { 30 << 1,        0},  //  22: 0000 001x
	{       0,   0x0d01}, {       0,   0x0006},  //  23: 0010 000x
	{       0,   0x0c01}, {       0,   0x0b01},  //  24: 0010 001x
	{       0,   0x0302}, {       0,   0x0103},  //  25: 0010 010x
	{       0,   0x0005}, {       0,   0x0a01},  //  26: 0010 011x
	{ 31 << 1,        0}, { 32 << 1,        0},  //  27: 0000 0000x
	{ 33 << 1,        0}, { 34 << 1,        0},  //  28: 0000 0001x
	{ 35 << 1,        0}, { 36 << 1,        0},  //  29: 0000 0010x
	{ 37 << 1,        0}, { 38 << 1,        0},  //  30: 0000 0011x
	{ 39 << 1,        0}, { 40 << 1,        0},  //  31: 0000 0000 0x
	{ 41 << 1,        0}, { 42 << 1,        0},  //  32: 0000 0000 1x
	{ 43 << 1,        0}, { 44 << 1,        0},  //  33: 0000 0001 0x
	{ 45 << 1,        0}, { 46 << 1,        0},  //  34: 0000 0001 1x
	{       0,   0x1001}, {       0,   0x0502},  //  35: 0000 0010 0x
	{       0,   0x0007}, {       0,   0x0203},  //  36: 0000 0010 1x
	{       0,   0x0104}, {       0,   0x0f01},  //  37: 0000 0011 0x
	{       0,   0x0e01}, {       0,   0x0402},  //  38: 0000 0011 1x
	{ 47 << 1,        0}, { 48 << 1,        0},  //  39: 0000 0000 00x
	{ 49 << 1,        0}, { 50 << 1,        0},  //  40: 0000 0000 01x
	{ 51 << 1,        0}, { 52 << 1,        0},  //  41: 0000 0000 10x
	{ 53 << 1,        0}, { 54 << 1,        0},  //  42: 0000 0000 11x
	{ 55 << 1,        0}, { 56 << 1,        0},  //  43: 0000 0001 00x
	{ 57 << 1,        0}, { 58 << 1,        0},  //  44: 0000 0001 01x
	{ 59 << 1,        0}, { 60 << 1,        0},  //  45: 0000 0001 10x
	{ 61 << 1,        0}, { 62 << 1,        0},  //  46: 0000 0001 11x
	{      -1,        0}, { 63 << 1,        0},  //  47: 0000 0000 000x
	{ 64 << 1,        0}, { 65 << 1,        0},  //  48: 0000 0000 001x
	{ 66 << 1,        0}, { 67 << 1,        0},  //  49: 0000 0000 010x
	{ 68 << 1,        0}, { 69 << 1,        0},  //  50: 0000 0000 011x
	{ 70 << 1,        0}, { 71 << 1,        0},  //  51: 0000 0000 100x
	{ 72 << 1,        0}, { 73 << 1,        0},  //  52: 0000 0000 101x
	{ 74 << 1,        0}, { 75 << 1,        0},  //  53: 0000 0000 110x
	{ 76 << 1,        0}, { 77 << 1,        0},  //  54: 0000 0000 111x
	{       0,   0x000b}, {       0,   0x0802},  //  55: 0000 0001 000x
	{       0,   0x0403}, {       0,   0x000a},  //  56: 0000 0001 001x
	{       0,   0x0204}, {       0,   0x0702},  //  57: 0000 0001 010x
	{       0,   0x1501}, {       0,   0x1401},  //  58: 0000 0001 011x
	{       0,   0x0009}, {       0,   0x1301},  //  59: 0000 0001 100x
	{       0,   0x1201}, {       0,   0x0105},  //  60: 0000 0001 101x
	{       0,   0x0303}, {       0,   0x0008},  //  61: 0000 0001 110x
	{       0,   0x0602}, {       0,   0x1101},  //  62: 0000 0001 111x
	{ 78 << 1,        0}, { 79 << 1,        0},  //  63: 0000 0000 0001x
	{ 80 << 1,        0}, { 81 << 1,        0},  //  64: 0000 0000 0010x
	{ 82 << 1,        0}, { 83 << 1,        0},  //  65: 0000 0000 0011x
	{ 84 << 1,        0}, { 85 << 1,        0},  //  66: 0000 0000 0100x
	{ 86 << 1,        0}, { 87 << 1,        0},  //  67: 0000 0000 0101x
	{ 88 << 1,        0}, { 89 << 1,        0},  //  68: 0000 0000 0110x
	{ 90 << 1,        0}, { 91 << 1,        0},  //  69: 0000 0000 0111x
	{       0,   0x0a02}, {       0,   0x0902},  //  70: 0000 0000 1000x
	{       0,   0x0503}, {       0,   0x0304},  //  71: 0000 0000 1001x
	{       0,   0x0205}, {       0,   0x0107},  //  72: 0000 0000 1010x
	{       0,   0x0106}, {       0,   0x000f},  //  73: 0000 0000 1011x
	{       0,   0x000e}, {       0,   0x000d},  //  74: 0000 0000 1100x
	{       0,   0x000c}, {       0,   0x1a01},  //  75: 0000 0000 1101x
	{       0,   0x1901}, {       0,   0x1801},  //  76: 0000 0000 1110x
	{       0,   0x1701}, {       0,   0x1601},  //  77: 0000 0000 1111x
	{ 92 << 1,        0}, { 93 << 1,        0},  //  78: 0000 0000 0001 0x
	{ 94 << 1,        0}, { 95 << 1,        0},  //  79: 0000 0000 0001 1x
	{ 96 << 1,        0}, { 97 << 1,        0},  //  80: 0000 0000 0010 0x
	{ 98 << 1,        0}, { 99 << 1,        0},  //  81: 0000 0000 0010 1x
	{100 << 1,        0}, {101 << 1,        0},  //  82: 0000 0000 0011 0x
	{102 << 1,        0}, {103 << 1,        0},  //  83: 0000 0000 0011 1x
	{       0,   0x001f}, {       0,   0x001e},  //  84: 0000 0000 0100 0x
	{       0,   0x001d}, {       0,   0x001c},  //  85: 0000 0000 0100 1x
	{       0,   0x001b}, {       0,   0x001a},  //  86: 0000 0000 0101 0x
	{       0,   0x0019}, {       0,   0x0018},  //  87: 0000 0000 0101 1x
	{       0,   0x0017}, {       0,   0x0016},  //  88: 0000 0000 0110 0x
	{       0,   0x0015}, {       0,   0x0014},  //  89: 0000 0000 0110 1x
	{       0,   0x0013}, {       0,   0x0012},  //  90: 0000 0000 0111 0x
	{       0,   0x0011}, {       0,   0x0010},  //  91: 0000 0000 0111 1x
	{104 << 1,        0}, {105 << 1,        0},  //  92: 0000 0000 0001 00x
	{106 << 1,        0}, {107 << 1,        0},  //  93: 0000 0000 0001 01x
	{108 << 1,        0}, {109 << 1,        0},  //  94: 0000 0000 0001 10x
	{110 << 1,        0}, {111 << 1,        0},  //  95: 0000 0000 0001 11x
	{       0,   0x0028}, {       0,   0x0027},  //  96: 0000 0000 0010 00x
	{       0,   0x0026}, {       0,   0x0025},  //  97: 0000 0000 0010 01x
	{       0,   0x0024}, {       0,   0x0023},  //  98: 0000 0000 0010 10x
	{       0,   0x0022}, {       0,   0x0021},  //  99: 0000 0000 0010 11x
	{       0,   0x0020}, {       0,   0x010e},  // 100: 0000 0000 0011 00x
	{       0,   0x010d}, {       0,   0x010c},  // 101: 0000 0000 0011 01x
	{       0,   0x010b}, {       0,   0x010a},  // 102: 0000 0000 0011 10x
	{       0,   0x0109}, {       0,   0x0108},  // 103: 0000 0000 0011 11x
	{       0,   0x0112}, {       0,   0x0111},  // 104: 0000 0000 0001 000x
	{       0,   0x0110}, {       0,   0x010f},  // 105: 0000 0000 0001 001x
	{       0,   0x0603}, {       0,   0x1002},  // 106: 0000 0000 0001 010x
	{       0,   0x0f02}, {       0,   0x0e02},  // 107: 0000 0000 0001 011x
	{       0,   0x0d02}, {       0,   0x0c02},  // 108: 0000 0000 0001 100x
	{       0,   0x0b02}, {       0,   0x1f01},  // 109: 0000 0000 0001 101x
	{       0,   0x1e01}, {       0,   0x1d01},  // 110: 0000 0000 0001 110x
	{       0,   0x1c01}, {       0,   0x1b01},  // 111: 0000 0000 0001 111x
};

typedef struct {
	int full_px;
	int is_set;
	int r_size;
	int h;
	int v;
} plm_video_motion_t;

struct plm_video_t {
	int frames_decoded;
	int width;
	int height;
	int mb_width;
	int mb_height;
	int mb_size;

	int luma_width;
	int luma_height;

	int chroma_width;
	int chroma_height;

	int start_code;
	int picture_type;

	plm_video_motion_t motion_forward;
	plm_video_motion_t motion_backward;

	int has_sequence_header;

	int quantizer_scale;
	int slice_begin;
	int macroblock_address;

	int mb_row;
	int mb_col;

	int macroblock_type;
	int macroblock_intra;

	int dc_predictor[3];

	plm_buffer_t *buffer;
	int destroy_buffer_when_done;

	plm_frame_t frame_current;
	plm_frame_t frame_forward;
	plm_frame_t frame_backward;

	uint8_t *frames_data;

	int block_data[64];
	// AmigaMCE: where block_data has coefficients (zeroed there alone
	// after its IDCT): the columns and rows they are in, and their places
	// (64 or more: all zeroed)
	unsigned int block_cols, block_rows;
	int block_n;
	uint8_t block_at[64];
	uint8_t intra_quant_matrix[64];
	uint8_t non_intra_quant_matrix[64];

	int has_reference_frame;
	int assume_no_b_frames;

	// mpegv: the sequence header's codes as stored
	int rate_code;
	int aspect_code;
	int bit_rate;

	// AmigaMCE: B pictures passed over (plm_video_set_skip_b)
	int skip_b;
	int b_skipped;
	unsigned int b_skipped_count;
};

static inline uint8_t plm_clamp(int n) {
	// AmigaMCE: one comparison when in range (most are)
	if ((unsigned int)n > 255) {
		n = n < 0 ? 0 : 255;
	}
	return n;
}

// AmigaMCE: value added to 8 pixels at q, each clamped (plm_clamp(q[k] +
// value)), a longword (4 pixels) at a time: per byte, a saturating add or
// subtract, no carry or borrow reaching the next (each byte's low 7 bits'
// sum is under 256; each byte's difference from x | 0x80 at least 1).
static inline uint32_t plm_add_sat4(uint32_t x, uint32_t y) {
	uint32_t s7 = (x & 0x7f7f7f7fu) + (y & 0x7f7f7f7fu);
	uint32_t s = s7 ^ ((x ^ y) & 0x80808080u);
	uint32_t c = ((x & y) | ((x | y) & ~s)) & 0x80808080u;
	return s | ((c << 1) - (c >> 7));
}
static inline uint32_t plm_sub_sat4(uint32_t x, uint32_t y) {
	uint32_t d7 = (x | 0x80808080u) - (y & 0x7f7f7f7fu);
	uint32_t d = d7 ^ ((x ^ ~y) & 0x80808080u);
	uint32_t b = ((~x & y) | (~(x ^ y) & d)) & 0x80808080u;
	return d & ~((b << 1) - (b >> 7));
}
#if defined(__GNUC__)
__attribute__((always_inline))
#endif
static inline void plm_add_const8(uint8_t *q, int value) {
	if (value >= 0) {
		if (value > 255) {
			PLM_ST32(q, 0xffffffffu);
			PLM_ST32(q + 4, 0xffffffffu);
		}
		else {
			uint32_t y = (uint32_t)value * 0x01010101u;
			PLM_ST32(q, plm_add_sat4(PLM_LD32(q), y));
			PLM_ST32(q + 4, plm_add_sat4(PLM_LD32(q + 4), y));
		}
	}
	else {
		if (value < -255) {
			PLM_ST32(q, 0);
			PLM_ST32(q + 4, 0);
		}
		else {
			uint32_t y = (uint32_t)(-value) * 0x01010101u;
			PLM_ST32(q, plm_sub_sat4(PLM_LD32(q), y));
			PLM_ST32(q + 4, plm_sub_sat4(PLM_LD32(q + 4), y));
		}
	}
}

// AmigaMCE: x * 362 and x * 196 by shifts and adds (the same products:
// a 68020's MULS.L takes some 45 cycles, these some 30 and 20); the empty
// asm keeps the compiler from making them a multiply again.
#if defined(__GNUC__)
#define PLM_KEEP(x) __asm__("" : "+r"(x))
#else
#define PLM_KEEP(x)
#endif
static inline int plm_mul362(int x) {
	if (__builtin_constant_p(x)) {
		return x * 362;
	}
	int t = x + (int)((unsigned int)x << 2);       // 5x
	PLM_KEEP(t);
	t = t + (int)((unsigned int)t << 3);           // 45x
	PLM_KEEP(t);
	t = x + (int)((unsigned int)t << 2);           // 181x
	return t + t;
}
static inline int plm_mul196(int x) {
	if (__builtin_constant_p(x)) {
		return x * 196;
	}
	int t = x + (int)((unsigned int)x << 1);       // 3x
	PLM_KEEP(t);
	t = x + (int)((unsigned int)t << 4);           // 49x
	PLM_KEEP(t);
	return (int)((unsigned int)t << 2);
}

int plm_video_decode_sequence_header(plm_video_t *self);
void plm_video_init_frame(plm_video_t *self, plm_frame_t *frame, uint8_t *base);
void plm_video_decode_picture(plm_video_t *self);
void plm_video_decode_slice(plm_video_t *self, int slice);
void plm_video_decode_macroblock(plm_video_t *self);
void plm_video_decode_motion_vectors(plm_video_t *self);
int plm_video_decode_motion_vector(plm_video_t *self, int r_size, int motion);
void plm_video_predict_macroblock(plm_video_t *self);
void plm_video_copy_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v);
void plm_video_interpolate_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v);
void plm_video_process_macroblock(plm_video_t *self, uint8_t *s, uint8_t *d, int mh, int mb, int bs, int interp);
void plm_video_decode_block(plm_video_t *self, int block);
void plm_video_idct(int *block);
void plm_video_idct_put(plm_video_t *self, uint8_t *d, int dw, int intra);

// AmigaMCE: the video VLCs' tables of their first bits (plm_vlc_fast_t),
// made once.
#define PLM_VLC_FAST_BITS 8
#define PLM_VLC_FAST_COEFF_BITS 10
static plm_vlc_fast_t plm_fast_mb_increment[1 << PLM_VLC_FAST_BITS];
static plm_vlc_fast_t plm_fast_mb_type[4][1 << PLM_VLC_FAST_BITS];
static plm_vlc_fast_t plm_fast_cbp[1 << PLM_VLC_FAST_BITS];
static plm_vlc_fast_t plm_fast_motion[1 << PLM_VLC_FAST_BITS];
static plm_vlc_fast_t plm_fast_dct_size[2][1 << PLM_VLC_FAST_BITS];
static plm_vlc_fast_t plm_fast_dct_coeff[1 << PLM_VLC_FAST_COEFF_BITS];
static int plm_fast_ready;

static void plm_video_fast_init(void) {
	if (plm_fast_ready) {
		return;
	}
	plm_vlc_fast_build(PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, plm_fast_mb_increment, PLM_VLC_FAST_BITS);
	for (int t = 1; t < 4; t++) {
		plm_vlc_fast_build(PLM_VIDEO_MACROBLOCK_TYPE[t], plm_fast_mb_type[t], PLM_VLC_FAST_BITS);
	}
	plm_vlc_fast_build(PLM_VIDEO_CODE_BLOCK_PATTERN, plm_fast_cbp, PLM_VLC_FAST_BITS);
	plm_vlc_fast_build(PLM_VIDEO_MOTION, plm_fast_motion, PLM_VLC_FAST_BITS);
	plm_vlc_fast_build(PLM_VIDEO_DCT_SIZE_LUMINANCE, plm_fast_dct_size[0], PLM_VLC_FAST_BITS);
	plm_vlc_fast_build(PLM_VIDEO_DCT_SIZE_CHROMINANCE, plm_fast_dct_size[1], PLM_VLC_FAST_BITS);
	plm_vlc_fast_build((const plm_vlc_t *)PLM_VIDEO_DCT_COEFF, plm_fast_dct_coeff, PLM_VLC_FAST_COEFF_BITS);
	plm_fast_ready = TRUE;
}

plm_video_t * plm_video_create_with_buffer(plm_buffer_t *buffer, int destroy_when_done) {
	plm_video_fast_init();
	plm_video_t *self = (plm_video_t *)PLM_MALLOC(sizeof(plm_video_t));
	memset(self, 0, sizeof(plm_video_t));
	
	self->buffer = buffer;
	self->destroy_buffer_when_done = destroy_when_done;

	// Attempt to decode the sequence header
	self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_SEQUENCE);
	if (self->start_code != -1) {
		plm_video_decode_sequence_header(self);
	}
	return self;
}

void plm_video_destroy(plm_video_t *self) {
	if (self->destroy_buffer_when_done) {
		plm_buffer_destroy(self->buffer);
	}

	if (self->has_sequence_header) {
		PLM_FREE(self->frames_data);
	}

	PLM_FREE(self);
}

int plm_video_get_width(plm_video_t *self) {
	return plm_video_has_header(self)
		? self->width
		: 0;
}

int plm_video_get_height(plm_video_t *self) {
	return plm_video_has_header(self)
		? self->height
		: 0;
}

void plm_video_set_no_delay(plm_video_t *self, int no_delay) {
	self->assume_no_b_frames = no_delay;
}

void plm_video_set_skip_b(plm_video_t *self, int skip) {
	self->skip_b = skip;
}

unsigned int plm_video_get_skipped_b(plm_video_t *self) {
	return self->b_skipped_count;
}

void plm_video_rewind(plm_video_t *self) {
	plm_buffer_rewind(self->buffer);
	self->frames_decoded = 0;
	self->has_reference_frame = FALSE;
	self->start_code = -1;
}

int plm_video_has_ended(plm_video_t *self) {
	return plm_buffer_has_ended(self->buffer);
}

plm_frame_t *plm_video_decode(plm_video_t *self) {
	if (!plm_video_has_header(self)) {
		return NULL;
	}
	
	plm_frame_t *frame = NULL;
	do {
		if (self->start_code != PLM_START_PICTURE) {
			self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_PICTURE);
			
			if (self->start_code == -1) {
				// If we reached the end of the file and the previously decoded
				// frame was a reference frame, we still have to return it.
				if (
					self->has_reference_frame &&
					!self->assume_no_b_frames &&
					plm_buffer_has_ended(self->buffer) && (
						self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
						self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
					)
				) {
					self->has_reference_frame = FALSE;
					frame = &self->frame_backward;
					break;
				}

				return NULL;
			}
		}

		// Make sure we have a full picture in the buffer before attempting to
		// decode it. Sadly, this can only be done by seeking for the start code
		// of the next picture. Also, if we didn't find the start code for the
		// next picture, but the source has ended, we assume that this last
		// picture is in the buffer.
		if (
			plm_buffer_has_start_code(self->buffer, PLM_START_PICTURE) == -1 &&
			!plm_buffer_has_ended(self->buffer)
		) {
			return NULL;
		}
		plm_buffer_discard_read_bytes(self->buffer);
		
		plm_video_decode_picture(self);

		if (self->b_skipped) {
			// AmigaMCE: a B picture passed over: its time goes by, no frame
			self->b_skipped = FALSE;
			self->b_skipped_count++;
			self->frames_decoded++;
			continue;
		}
		if (self->assume_no_b_frames) {
			frame = &self->frame_backward;
		}
		else if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
			frame = &self->frame_current;
		}
		else if (self->has_reference_frame) {
			frame = &self->frame_forward;
		}
		else {
			self->has_reference_frame = TRUE;
		}
	} while (!frame);
	
	frame->index = self->frames_decoded;	// mpegv: its time is the caller's, from this
	self->frames_decoded++;
	
	return frame;
}

int plm_video_has_header(plm_video_t *self) {
	if (self->has_sequence_header) {
		return TRUE;
	}

	if (self->start_code != PLM_START_SEQUENCE) {
		self->start_code = plm_buffer_find_start_code(self->buffer, PLM_START_SEQUENCE);
	}
	if (self->start_code == -1) {
		return FALSE;
	}
	
	if (!plm_video_decode_sequence_header(self)) {
		return FALSE;
	}

	return TRUE;
}

int plm_video_decode_sequence_header(plm_video_t *self) {
	int max_header_size = 64 + 2 * 64 * 8; // 64 bit header + 2x 64 byte matrix
	if (!plm_buffer_has(self->buffer, max_header_size)) {
		return FALSE;
	}

	self->width = plm_buffer_read(self->buffer, 12);
	self->height = plm_buffer_read(self->buffer, 12);

	if (self->width <= 0 || self->height <= 0) {
		return FALSE;
	}

	// Get pixel aspect ratio
	self->aspect_code = plm_buffer_read(self->buffer, 4);	// mpegv: as stored, no table

	// Get frame rate
	self->rate_code = plm_buffer_read(self->buffer, 4);	// mpegv: as stored, no table

	// Skip bit_rate, marker, buffer_size and constrained bit
	self->bit_rate = plm_buffer_read(self->buffer, 18);	// mpegv: kept (400 bit/s units)
	plm_buffer_skip(self->buffer, 1 + 10 + 1);

	// Load custom intra quant matrix?
	if (plm_buffer_read(self->buffer, 1)) { 
		for (int i = 0; i < 64; i++) {
			int idx = PLM_VIDEO_ZIG_ZAG[i];
			self->intra_quant_matrix[idx] = plm_buffer_read(self->buffer, 8);
		}
	}
	else {
		memcpy(self->intra_quant_matrix, PLM_VIDEO_INTRA_QUANT_MATRIX, 64);
	}

	// Load custom non intra quant matrix?
	if (plm_buffer_read(self->buffer, 1)) { 
		for (int i = 0; i < 64; i++) {
			int idx = PLM_VIDEO_ZIG_ZAG[i];
			self->non_intra_quant_matrix[idx] = plm_buffer_read(self->buffer, 8);
		}
	}
	else {
		memcpy(self->non_intra_quant_matrix, PLM_VIDEO_NON_INTRA_QUANT_MATRIX, 64);
	}

	self->mb_width = (self->width + 15) >> 4;
	self->mb_height = (self->height + 15) >> 4;
	self->mb_size = self->mb_width * self->mb_height;

	self->luma_width = self->mb_width << 4;
	self->luma_height = self->mb_height << 4;

	self->chroma_width = self->mb_width << 3;
	self->chroma_height = self->mb_height << 3;


	// Allocate one big chunk of data for all 3 frames = 9 planes
	size_t luma_plane_size = self->luma_width * self->luma_height;
	size_t chroma_plane_size = self->chroma_width * self->chroma_height;
	size_t frame_data_size = (luma_plane_size + 2 * chroma_plane_size);

	self->frames_data = (uint8_t*)PLM_MALLOC(frame_data_size * 3);
	plm_video_init_frame(self, &self->frame_current, self->frames_data + frame_data_size * 0);
	plm_video_init_frame(self, &self->frame_forward, self->frames_data + frame_data_size * 1);
	plm_video_init_frame(self, &self->frame_backward, self->frames_data + frame_data_size * 2);

	self->has_sequence_header = TRUE;
	return TRUE;
}

void plm_video_init_frame(plm_video_t *self, plm_frame_t *frame, uint8_t *base) {
	size_t luma_plane_size = self->luma_width * self->luma_height;
	size_t chroma_plane_size = self->chroma_width * self->chroma_height;

	frame->width = self->width;
	frame->height = self->height;
	frame->y.width = self->luma_width;
	frame->y.height = self->luma_height;
	frame->y.data = base;

	frame->cr.width = self->chroma_width;
	frame->cr.height = self->chroma_height;
	frame->cr.data = base + luma_plane_size;

	frame->cb.width = self->chroma_width;
	frame->cb.height = self->chroma_height;
	frame->cb.data = base + luma_plane_size + chroma_plane_size;
}

void plm_video_decode_picture(plm_video_t *self) {
	plm_buffer_skip(self->buffer, 10); // skip temporalReference
	self->picture_type = plm_buffer_read(self->buffer, 3);
	self->frame_current.type = self->picture_type;	// mpegv: carried with the frame
	plm_buffer_skip(self->buffer, 16); // skip vbv_delay

	// D frames or unknown coding type
	if (self->picture_type <= 0 || self->picture_type > PLM_VIDEO_PICTURE_TYPE_B) {
		return;
	}

	// AmigaMCE: a B picture passed over (only once a reference frame is
	// shown, and not in "no delay" mode, where every frame is returned):
	// its slices not read, the next picture found from here
	if (
		self->skip_b && self->picture_type == PLM_VIDEO_PICTURE_TYPE_B &&
		self->has_reference_frame && !self->assume_no_b_frames
	) {
		self->b_skipped = TRUE;
		self->start_code = -1;
		return;
	}

	// Forward full_px, f_code
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_B
	) {
		self->motion_forward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0) {
			// Ignore picture with zero f_code
			return;
		}
		self->motion_forward.r_size = f_code - 1;
	}

	// Backward full_px, f_code
	if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
		self->motion_backward.full_px = plm_buffer_read(self->buffer, 1);
		int f_code = plm_buffer_read(self->buffer, 3);
		if (f_code == 0) {
			// Ignore picture with zero f_code
			return;
		}
		self->motion_backward.r_size = f_code - 1;
	}

	plm_frame_t frame_temp = self->frame_forward;
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
	) {
		self->frame_forward = self->frame_backward;
	}


	// Find first slice start code; skip extension and user data
	do {
		self->start_code = plm_buffer_next_start_code(self->buffer);
	} while (
		self->start_code == PLM_START_EXTENSION || 
		self->start_code == PLM_START_USER_DATA
	);

	// Decode all slices
	while (PLM_START_IS_SLICE(self->start_code)) {
		plm_video_decode_slice(self, self->start_code & 0x000000FF);
		if (self->macroblock_address >= self->mb_size - 1) {
			break;
		}
		self->start_code = plm_buffer_next_start_code(self->buffer);
	}

	// If this is a reference picture rotate the prediction pointers
	if (
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_INTRA ||
		self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE
	) {
		self->frame_backward = self->frame_current;
		self->frame_current = frame_temp;
	}
}

void plm_video_decode_slice(plm_video_t *self, int slice) {
	self->slice_begin = TRUE;
	self->macroblock_address = (slice - 1) * self->mb_width - 1;

	// Reset motion vectors and DC predictors
	self->motion_backward.h = self->motion_forward.h = 0;
	self->motion_backward.v = self->motion_forward.v = 0;
	self->dc_predictor[0] = 128;
	self->dc_predictor[1] = 128;
	self->dc_predictor[2] = 128;

	self->quantizer_scale = plm_buffer_read(self->buffer, 5);

	// Skip extra
	while (plm_buffer_read(self->buffer, 1)) {
		plm_buffer_skip(self->buffer, 8);
	}

	do {
		plm_video_decode_macroblock(self);
	} while (
		self->macroblock_address < self->mb_size - 1 &&
		plm_buffer_peek_non_zero(self->buffer, 23)
	);
}

void plm_video_decode_macroblock(plm_video_t *self) {
	// Decode increment
	int increment = 0;
	int t = plm_buffer_read_vlc_fast(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, plm_fast_mb_increment, PLM_VLC_FAST_BITS);

	while (t == 34) {
		// macroblock_stuffing
		t = plm_buffer_read_vlc_fast(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, plm_fast_mb_increment, PLM_VLC_FAST_BITS);
	}
	while (t == 35) {
		// macroblock_escape
		increment += 33;
		t = plm_buffer_read_vlc_fast(self->buffer, PLM_VIDEO_MACROBLOCK_ADDRESS_INCREMENT, plm_fast_mb_increment, PLM_VLC_FAST_BITS);
	}
	increment += t;

	// Process any skipped macroblocks
	if (self->slice_begin) {
		// The first increment of each slice is relative to beginning of the
		// previous row, not the previous macroblock
		self->slice_begin = FALSE;
		self->macroblock_address += increment;
	}
	else {
		if (self->macroblock_address + increment >= self->mb_size) {
			return; // invalid
		}
		if (increment > 1) {
			// Skipped macroblocks reset DC predictors
			self->dc_predictor[0] = 128;
			self->dc_predictor[1] = 128;
			self->dc_predictor[2] = 128;

			// Skipped macroblocks in P-pictures reset motion vectors
			if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
				self->motion_forward.h = 0;
				self->motion_forward.v = 0;
			}
		}

		// Predict skipped macroblocks
		while (increment > 1) {
			self->macroblock_address++;
			self->mb_row = self->macroblock_address / self->mb_width;
			self->mb_col = self->macroblock_address % self->mb_width;

			plm_video_predict_macroblock(self);
			increment--;
		}
		self->macroblock_address++;
	}

	self->mb_row = self->macroblock_address / self->mb_width;
	self->mb_col = self->macroblock_address % self->mb_width;

	if (self->mb_col >= self->mb_width || self->mb_row >= self->mb_height) {
		return; // corrupt stream;
	}

	// Process the current macroblock
	const plm_vlc_t *table = PLM_VIDEO_MACROBLOCK_TYPE[self->picture_type];
	self->macroblock_type = self->picture_type >= 1 && self->picture_type <= 3
		? plm_buffer_read_vlc_fast(self->buffer, table, plm_fast_mb_type[self->picture_type], PLM_VLC_FAST_BITS)
		: plm_buffer_read_vlc(self->buffer, table);

	self->macroblock_intra = (self->macroblock_type & 0x01);
	self->motion_forward.is_set = (self->macroblock_type & 0x08);
	self->motion_backward.is_set = (self->macroblock_type & 0x04);

	// Quantizer scale
	if ((self->macroblock_type & 0x10) != 0) {
		self->quantizer_scale = plm_buffer_read(self->buffer, 5);
	}

	if (self->macroblock_intra) {
		// Intra-coded macroblocks reset motion vectors
		self->motion_backward.h = self->motion_forward.h = 0;
		self->motion_backward.v = self->motion_forward.v = 0;
	}
	else {
		// Non-intra macroblocks reset DC predictors
		self->dc_predictor[0] = 128;
		self->dc_predictor[1] = 128;
		self->dc_predictor[2] = 128;

		plm_video_decode_motion_vectors(self);
		plm_video_predict_macroblock(self);
	}

	// Decode blocks
	int cbp = ((self->macroblock_type & 0x02) != 0)
		? plm_buffer_read_vlc_fast(self->buffer, PLM_VIDEO_CODE_BLOCK_PATTERN, plm_fast_cbp, PLM_VLC_FAST_BITS)
		: (self->macroblock_intra ? 0x3f : 0);

	for (int block = 0, mask = 0x20; block < 6; block++) {
		if ((cbp & mask) != 0) {
			plm_video_decode_block(self, block);
		}
		mask >>= 1;
	}
}

void plm_video_decode_motion_vectors(plm_video_t *self) {

	// Forward
	if (self->motion_forward.is_set) {
		int r_size = self->motion_forward.r_size;
		self->motion_forward.h = plm_video_decode_motion_vector(self, r_size, self->motion_forward.h);
		self->motion_forward.v = plm_video_decode_motion_vector(self, r_size, self->motion_forward.v);
	}
	else if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_PREDICTIVE) {
		// No motion information in P-picture, reset vectors
		self->motion_forward.h = 0;
		self->motion_forward.v = 0;
	}

	if (self->motion_backward.is_set) {
		int r_size = self->motion_backward.r_size;
		self->motion_backward.h = plm_video_decode_motion_vector(self, r_size, self->motion_backward.h);
		self->motion_backward.v = plm_video_decode_motion_vector(self, r_size, self->motion_backward.v);
	}
}

int plm_video_decode_motion_vector(plm_video_t *self, int r_size, int motion) {
	int fscale = 1 << r_size;
	int m_code = plm_buffer_read_vlc_fast(self->buffer, PLM_VIDEO_MOTION, plm_fast_motion, PLM_VLC_FAST_BITS);
	int r = 0;
	int d;

	if ((m_code != 0) && (fscale != 1)) {
		r = plm_buffer_read(self->buffer, r_size);
		d = ((abs(m_code) - 1) << r_size) + r + 1;
		if (m_code < 0) {
			d = -d;
		}
	}
	else {
		d = m_code;
	}

	motion += d;
	if (motion > (fscale << 4) - 1) {
		motion -= fscale << 5;
	}
	else if (motion < (int)((unsigned)(-fscale) << 4)) {
		motion += fscale << 5;
	}

	return motion;
}

void plm_video_predict_macroblock(plm_video_t *self) {
	int fw_h = self->motion_forward.h;
	int fw_v = self->motion_forward.v;

	if (self->motion_forward.full_px) {
		fw_h <<= 1;
		fw_v <<= 1;
	}

	if (self->picture_type == PLM_VIDEO_PICTURE_TYPE_B) {
		int bw_h = self->motion_backward.h;
		int bw_v = self->motion_backward.v;

		if (self->motion_backward.full_px) {
			bw_h <<= 1;
			bw_v <<= 1;
		}

		if (self->motion_forward.is_set) {
			plm_video_copy_macroblock(self, &self->frame_forward, fw_h, fw_v);
			if (self->motion_backward.is_set) {
				plm_video_interpolate_macroblock(self, &self->frame_backward, bw_h, bw_v);
			}
		}
		else {
			plm_video_copy_macroblock(self, &self->frame_backward, bw_h, bw_v);
		}
	}
	else {
		plm_video_copy_macroblock(self, &self->frame_forward, fw_h, fw_v);
	}
}

void plm_video_copy_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v) {
	plm_frame_t *d = &self->frame_current;
	plm_video_process_macroblock(self, s->y.data, d->y.data, motion_h, motion_v, 16, FALSE);
	plm_video_process_macroblock(self, s->cr.data, d->cr.data, motion_h / 2, motion_v / 2, 8, FALSE);
	plm_video_process_macroblock(self, s->cb.data, d->cb.data, motion_h / 2, motion_v / 2, 8, FALSE);
}

void plm_video_interpolate_macroblock(plm_video_t *self, plm_frame_t *s, int motion_h, int motion_v) {
	plm_frame_t *d = &self->frame_current;
	plm_video_process_macroblock(self, s->y.data, d->y.data, motion_h, motion_v, 16, TRUE);
	plm_video_process_macroblock(self, s->cr.data, d->cr.data, motion_h / 2, motion_v / 2, 8, TRUE);
	plm_video_process_macroblock(self, s->cb.data, d->cb.data, motion_h / 2, motion_v / 2, 8, TRUE);
}

#define PLM_BLOCK_SET(DEST, DEST_INDEX, DEST_WIDTH, SOURCE_INDEX, SOURCE_WIDTH, BLOCK_SIZE, OP) do { \
	int dest_scan = DEST_WIDTH - BLOCK_SIZE; \
	int source_scan = SOURCE_WIDTH - BLOCK_SIZE; \
	for (int y = 0; y < BLOCK_SIZE; y++) { \
		for (int x = 0; x < BLOCK_SIZE; x++) { \
			DEST[DEST_INDEX] = OP; \
			SOURCE_INDEX++; DEST_INDEX++; \
		} \
		SOURCE_INDEX += source_scan; \
		DEST_INDEX += dest_scan; \
	}} while(FALSE)

void plm_video_process_macroblock(
	plm_video_t *self, uint8_t *s, uint8_t *d,
	int motion_h, int motion_v, int block_size, int interpolate
) {
	int dw = self->mb_width * block_size;

	int hp = motion_h >> 1;
	int vp = motion_v >> 1;
	int odd_h = (motion_h & 1) == 1;
	int odd_v = (motion_v & 1) == 1;

	unsigned int si = ((self->mb_row * block_size) + vp) * dw + (self->mb_col * block_size) + hp;
	unsigned int di = (self->mb_row * dw + self->mb_col) * block_size;
	
	unsigned int max_address = (dw * (self->mb_height * block_size - block_size + 1) - block_size);
	if (si > max_address || di > max_address) {
		return; // corrupt video
	}

	// AmigaMCE: four pixels at a time, a longword each (read unaligned):
	// the same averages, rounded the same, worked out a byte apiece in
	// each longword (no carry between them): a and b's, (a + b + 1) >> 1;
	// four's, (a + b + c + d + 2) >> 2, from their top six bits and their
	// bottom two apart.
	#define PLM_AVG2(a, b) (((a) | (b)) - ((((a) ^ (b)) >> 1) & 0x7f7f7f7fu))
	#define PLM_AVG4(a, b, c, e) ( \
		(((a) >> 2) & 0x3f3f3f3fu) + (((b) >> 2) & 0x3f3f3f3fu) + \
		(((c) >> 2) & 0x3f3f3f3fu) + (((e) >> 2) & 0x3f3f3f3fu) + \
		(((((a) & 0x03030303u) + ((b) & 0x03030303u) + ((c) & 0x03030303u) + \
		   ((e) & 0x03030303u) + 0x02020202u) >> 2) & 0x03030303u))
	#define PLM_MB_ROWS(OP) \
		for (int y = 0; y < block_size; y++, sp += dw, dp += dw) { \
			for (int x = 0; x < block_size; x += 4) { \
				uint32_t v; \
				OP; \
				PLM_ST32(dp + x, v); \
			} \
		}
	const uint8_t *sp = s + si;
	uint8_t *dp = d + di;
	switch ((interpolate << 2) | (odd_h << 1) | (odd_v)) {
		case 0: PLM_MB_ROWS(v = PLM_LD32(sp + x)); break;
		case 1: PLM_MB_ROWS(v = PLM_AVG2(PLM_LD32(sp + x), PLM_LD32(sp + x + dw))); break;
		case 2: PLM_MB_ROWS(v = PLM_AVG2(PLM_LD32(sp + x), PLM_LD32(sp + x + 1))); break;
		case 3: PLM_MB_ROWS(v = PLM_AVG4(PLM_LD32(sp + x), PLM_LD32(sp + x + 1),
			PLM_LD32(sp + x + dw), PLM_LD32(sp + x + dw + 1))); break;
		case 4: PLM_MB_ROWS(uint32_t p = PLM_LD32(sp + x); uint32_t q = PLM_LD32(dp + x);
			v = PLM_AVG2(q, p)); break;
		case 5: PLM_MB_ROWS(uint32_t p = PLM_AVG2(PLM_LD32(sp + x), PLM_LD32(sp + x + dw));
			uint32_t q = PLM_LD32(dp + x); v = PLM_AVG2(q, p)); break;
		case 6: PLM_MB_ROWS(uint32_t p = PLM_AVG2(PLM_LD32(sp + x), PLM_LD32(sp + x + 1));
			uint32_t q = PLM_LD32(dp + x); v = PLM_AVG2(q, p)); break;
		case 7: PLM_MB_ROWS(uint32_t p = PLM_AVG4(PLM_LD32(sp + x), PLM_LD32(sp + x + 1),
			PLM_LD32(sp + x + dw), PLM_LD32(sp + x + dw + 1));
			uint32_t q = PLM_LD32(dp + x); v = PLM_AVG2(q, p)); break;
	}
	#undef PLM_MB_ROWS
	#undef PLM_AVG2
	#undef PLM_AVG4
}

void plm_video_decode_block(plm_video_t *self, int block) {

	int n = 0;
	uint8_t *quant_matrix;

	// Decode DC coefficient of intra-coded blocks
	if (self->macroblock_intra) {
		int predictor;
		int dct_size;

		// DC prediction
		int plane_index = block > 3 ? block - 3 : 0;
		predictor = self->dc_predictor[plane_index];
		dct_size = plm_buffer_read_vlc_fast(self->buffer, PLM_VIDEO_DCT_SIZE[plane_index], plm_fast_dct_size[plane_index ? 1 : 0], PLM_VLC_FAST_BITS);

		// Read DC coeff
		if (dct_size > 0) {
			int differential = plm_buffer_read(self->buffer, dct_size);
			if ((differential & (1 << (dct_size - 1))) != 0) {
				self->block_data[0] = predictor + differential;
			}
			else {
				self->block_data[0] = predictor + (-(1 << dct_size) | (differential + 1));
			}
		}
		else {
			self->block_data[0] = predictor;
		}

		// Save predictor value
		self->dc_predictor[plane_index] = self->block_data[0];
		self->block_cols |= 1;          // (AmigaMCE: where it is)
		self->block_rows |= 1;
		if (self->block_n < 64) {
			self->block_at[self->block_n] = 0;
		}
		self->block_n++;

		// Dequantize + premultiply
		self->block_data[0] <<= (3 + 5);

		quant_matrix = self->intra_quant_matrix;
		n = 1;
	}
	else {
		quant_matrix = self->non_intra_quant_matrix;
	}

	// Decode AC coefficients (+DC for non-intra)
	// (AmigaMCE: as pl_mpeg did, with what it uses kept to hand; a code of
	// 10 bits or fewer read with the bit or two after it, its sign or end
	// of block's second, from one longword, where the buffer holds 4 more
	// bytes: 25 bits)
	plm_buffer_t *buffer = self->buffer;
	const int intra = self->macroblock_intra;
	const int quantizer_scale = self->quantizer_scale;
	int *block_data = self->block_data;
	unsigned int block_cols = self->block_cols, block_rows = self->block_rows;
	int block_n = self->block_n;
	int level = 0;
	while (TRUE) {
		int run = 0;
		uint16_t coeff;
		size_t bi = buffer->bit_index;
		size_t bytei = bi >> 3;
		if (buffer->length - bytei >= 4) {
			uint32_t w = PLM_BE32(buffer->bytes + bytei) << (bi & 7);
			const plm_vlc_fast_t *e = &plm_fast_dct_coeff[w >> (32 - PLM_VLC_FAST_COEFF_BITS)];
			int length = e->length;
			coeff = (uint16_t)e->value;
			if (length && coeff != 0xffff) {
				w <<= length;
				if (coeff == 0x0001 && n > 0) {
					length++;
					if (!(w & 0x80000000u)) {
						// end_of_block
						buffer->bit_index = bi + length;
						break;
					}
					w <<= 1;
				}
				run = coeff >> 8;
				level = coeff & 0xff;
				if (w & 0x80000000u) {
					level = -level;
				}
				buffer->bit_index = bi + length + 1;
				goto coded;
			}
		}
		coeff = (uint16_t)plm_buffer_read_vlc_fast(buffer, (const plm_vlc_t *)PLM_VIDEO_DCT_COEFF, plm_fast_dct_coeff, PLM_VLC_FAST_COEFF_BITS);

		if ((coeff == 0x0001) && (n > 0) && (plm_buffer_read(buffer, 1) == 0)) {
			// end_of_block
			break;
		}
		if (coeff == 0xffff) {
			// escape
			run = plm_buffer_read(buffer, 6);
			level = plm_buffer_read(buffer, 8);
			if (level == 0) {
				level = plm_buffer_read(buffer, 8);
			}
			else if (level == 128) {
				level = plm_buffer_read(buffer, 8) - 256;
			}
			else if (level > 128) {
				level = level - 256;
			}
		}
		else {
			run = coeff >> 8;
			level = coeff & 0xff;
			if (plm_buffer_read(buffer, 1)) {
				level = -level;
			}
		}

	coded:
		n += run;
		if (n < 0 || n >= 64) {
			self->block_cols = block_cols;
			self->block_rows = block_rows;
			self->block_n = block_n;
			return; // invalid
		}

		int de_zig_zagged = PLM_VIDEO_ZIG_ZAG[n];
		n++;

		// Dequantize, oddify, clip
		level = (unsigned)level << 1;
		if (!intra) {
			level += (level < 0 ? -1 : 1);
		}
		// (AmigaMCE: 16-bit multiplies, the same products: the level is
		// at most 513 either way, the scale 31 and the matrix's 255)
		level = ((int)(int16_t)((int)(int16_t)level * (int)(int16_t)quantizer_scale) *
			(int)(int16_t)quant_matrix[de_zig_zagged]) >> 4;
		if ((level & 1) == 0) {
			level -= level > 0 ? 1 : -1;
		}
		if (level > 2047) {
			level = 2047;
		}
		else if (level < -2048) {
			level = -2048;
		}

		// Save premultiplied coefficient (AmigaMCE: and where it is)
		block_data[de_zig_zagged] = (int)(int16_t)level *
			(int)(int16_t)PLM_VIDEO_PREMULTIPLIER_MATRIX[de_zig_zagged];
		block_cols |= 1u << (de_zig_zagged & 7);
		block_rows |= 1u << (de_zig_zagged >> 3);
		if (block_n < 64) {
			self->block_at[block_n] = (uint8_t)de_zig_zagged;
		}
		block_n++;
	}
	self->block_cols = block_cols;
	self->block_rows = block_rows;
	self->block_n = block_n;

	// Move block to its place
	uint8_t *d;
	int dw;
	int di;

	if (block < 4) {
		d = self->frame_current.y.data;
		dw = self->luma_width;
		di = (self->mb_row * self->luma_width + self->mb_col) << 4;
		if ((block & 1) != 0) {
			di += 8;
		}
		if ((block & 2) != 0) {
			di += self->luma_width << 3;
		}
	}
	else {
		d = (block == 4) ? self->frame_current.cb.data : self->frame_current.cr.data;
		dw = self->chroma_width;
		di = ((self->mb_row * self->luma_width) << 2) + (self->mb_col << 3);
	}

	int *s = self->block_data;
	if (n == 1) {
		// AmigaMCE: its DC alone, as pl_mpeg did it: a longword at a time
		// when intra
		int i;
		if (self->macroblock_intra) {
			uint32_t v = plm_clamp((s[0] + 128) >> 8) * 0x01010101u;
			for (i = 0; i < 8; i++, di += dw) {
				plm_u32_alias *q = (plm_u32_alias *)(void *)(d + di);
				q[0] = v;
				q[1] = v;
			}
		}
		else {
			int value = (s[0] + 128) >> 8;
			for (i = 0; i < 8; i++, di += dw) {
				plm_add_const8(d + di, value);
			}
		}
		s[0] = 0;
		if (self->block_n == 1) {
			// (nothing else there: if a block before it was given up,
			// its coefficients stay, as they did)
			self->block_cols = self->block_rows = 0;
			self->block_n = 0;
		}
		return;
	}
	plm_video_idct_put(self, d + di, dw, self->macroblock_intra);
}

// AmigaMCE: pl_mpeg's IDCT (plm_video_idct, below) in one direction, in
// its two halves: the even inputs' (c0, c2, c4, c6: y3 to y6) and the odd
// inputs' (c1, c3, c5, c7: b7, x4, x0, y7), with the same sums and
// roundings, each rounding within one half. A half of zeros gives zeros
// (each rounding of 0 is 0), so it is left out where its inputs are all
// zero, and given constant zeros the compiler leaves out what they add
// nothing to: the same numbers, sooner. PLM_IDCT_SPLIT: by which inputs
// may not be zero (PLM_IDCT_HALVES: each half's 0 none, 1 its first, 2
// its first two, 3 all four), A(k) each input, OUT(k, v) each result.
#define PLM_IDCT_EVEN(c0, c2, c4, c6) do { \
	int b1 = (c4); \
	int b3 = (c2) + (c6); \
	int m0 = (c0); \
	int x1 = m0 - b1; \
	int x2 = ((plm_mul362((c2) - (c6)) + 128) >> 8) - b3; \
	int x3 = m0 + b1; \
	y3 = x1 + x2; \
	y4 = x3 + b3; \
	y5 = x1 - x2; \
	y6 = x3 - b3; \
} while (0)

#define PLM_IDCT_ODD(c1, c3, c5, c7) do { \
	int b4 = (c5) - (c3); \
	int tmp1 = (c1) + (c7); \
	int tmp2 = (c3) + (c5); \
	int b6 = (c1) - (c7); \
	b7 = tmp1 + tmp2; \
	x4 = ((b6 * 473 - plm_mul196(b4) + 128) >> 8) - b7; \
	x0 = x4 - ((plm_mul362(tmp1 - tmp2) + 128) >> 8); \
	y7 = -x0 - ((b4 * 473 + plm_mul196(b6) + 128) >> 8); \
} while (0)

#define PLM_IDCT_HALVES(mask, ev, ov) do { \
	ev = !((mask) & 0x55) ? 0 : !((mask) & 0x54) ? 1 : !((mask) & 0x50) ? 2 : 3; \
	ov = !((mask) & 0xaa) ? 0 : !((mask) & 0xa8) ? 1 : !((mask) & 0xa0) ? 2 : 3; \
} while (0)

#define PLM_IDCT_SPLIT(ev, ov, A, OUT) do { \
	int y3, y4, y5, y6, b7, x4, x0, y7; \
	switch (ev) { \
		case 0: y3 = y4 = y5 = y6 = 0; break; \
		case 1: PLM_IDCT_EVEN(A(0), 0, 0, 0); break; \
		case 2: PLM_IDCT_EVEN(A(0), A(2), 0, 0); break; \
		default: PLM_IDCT_EVEN(A(0), A(2), A(4), A(6)); break; \
	} \
	switch (ov) { \
		case 0: b7 = x4 = x0 = y7 = 0; break; \
		case 1: PLM_IDCT_ODD(A(1), 0, 0, 0); break; \
		case 2: PLM_IDCT_ODD(A(1), A(3), 0, 0); break; \
		default: PLM_IDCT_ODD(A(1), A(3), A(5), A(7)); break; \
	} \
	OUT(0, b7 + y4); \
	OUT(1, x4 + y3); \
	OUT(2, y5 - x0); \
	OUT(3, y6 - y7); \
	OUT(4, y6 + y7); \
	OUT(5, x0 + y5); \
	OUT(6, y3 - x4); \
	OUT(7, y4 - b7); \
} while (0)

// AmigaMCE: LOOP(EV, OV) for the pair of halves ev, ov, as constants
#define PLM_IDCT_PAIRS(ev, ov, LOOP) do { \
	switch ((ev) * 4 + (ov)) { \
		case 0: LOOP(0, 0); break; \
		case 1: LOOP(0, 1); break; \
		case 2: LOOP(0, 2); break; \
		case 3: LOOP(0, 3); break; \
		case 4: LOOP(1, 0); break; \
		case 5: LOOP(1, 1); break; \
		case 6: LOOP(1, 2); break; \
		case 7: LOOP(1, 3); break; \
		case 8: LOOP(2, 0); break; \
		case 9: LOOP(2, 1); break; \
		case 10: LOOP(2, 2); break; \
		case 11: LOOP(2, 3); break; \
		case 12: LOOP(3, 0); break; \
		case 13: LOOP(3, 1); break; \
		case 14: LOOP(3, 2); break; \
		default: LOOP(3, 3); break; \
	} \
} while (0)

// AmigaMCE: the block's IDCT put into the picture at d (dw bytes a row):
// as plm_video_idct then setting (intra) or adding to the 8 x 8 pixels,
// clamped, but each column and row worked out from only the inputs that
// can be other than zero (the columns and rows its coefficients are in),
// the columns into t and the rows straight into place; then
// block_data's coefficients zeroed.
void plm_video_idct_put(plm_video_t *self, uint8_t *d, int dw, int intra) {
	int *s = self->block_data;
	unsigned int cols = self->block_cols, rows = self->block_rows;
	int t[64];
	int cev, cov, rev, rov, c, r;
	unsigned int read;

	// the columns, by the rows they have coefficients in; those the rows
	// will read but which have none, zeros. Each result of both directions
	// has the first input in it once, rounded nowhere on the way: so 128
	// added to the first column's first input is added once to every
	// row's first input and so to every result, and the rows' rounding,
	// (x + 128) >> 8, is then x >> 8 (the first column is always worked
	// out, and the even half, which has the first input, always: of
	// zeros, it gives zeros).
	PLM_IDCT_HALVES(rows, cev, cov);
	PLM_IDCT_HALVES(cols, rev, rov);
	if (cev == 0) {
		cev = 1;
	}
	if (rev == 0) {
		rev = 1;
	}
	read = (rev == 1 ? 0x01 : rev == 2 ? 0x05 : 0x55) |
		(rov == 0 ? 0 : rov == 1 ? 0x02 : rov == 2 ? 0x0a : 0xaa);
	// (each pair of halves a loop of its own: in each, which inputs are
	// zero is known, and what they would add left out)
	#define PLM_COL_OUT(k, v) o[(k) * 8] = (v)
	#define PLM_COL_IN(k) ((k) == 0 ? b[0] + round : b[(k) * 8])
	#define PLM_COL_LOOP(EV, OV) \
		for (c = 0; c < 8; c++) { \
			int *o = t + c; \
			int round = c == 0 ? 128 : 0; \
			if (cols & (1u << c)) { \
				const int *b = s + c; \
				PLM_IDCT_SPLIT(EV, OV, PLM_COL_IN, PLM_COL_OUT); \
			} \
			else if (read & (1u << c)) { \
				PLM_COL_OUT(0, round); PLM_COL_OUT(1, round); PLM_COL_OUT(2, round); \
				PLM_COL_OUT(3, round); PLM_COL_OUT(4, round); PLM_COL_OUT(5, round); \
				PLM_COL_OUT(6, round); PLM_COL_OUT(7, round); \
			} \
		}
	PLM_IDCT_PAIRS(cev, cov, PLM_COL_LOOP);
	#undef PLM_COL_LOOP
	#undef PLM_COL_IN
	#undef PLM_COL_OUT

	// the rows, rounded, into the picture
	#define PLM_ROW_IN(k) a[k]
	#define PLM_ROW_SET(k, v) d[k] = plm_clamp((v) >> 8)
	#define PLM_ROW_ADD(k, v) d[k] = plm_clamp(d[k] + ((v) >> 8))
	if (rows <= 1) {
		// coefficients in its first row alone: the columns each one number
		// all the way down, so the rows all the same: one worked out
		int v[8];
		const int *a = t;
		#define PLM_ROW_KEEP(k, x) v[k] = (x) >> 8
		PLM_IDCT_SPLIT(rev, rov, PLM_ROW_IN, PLM_ROW_KEEP);
		#undef PLM_ROW_KEEP
		if (intra) {
			uint8_t q[8];
			for (c = 0; c < 8; c++) {
				q[c] = plm_clamp(v[c]);
			}
			uint32_t q0 = PLM_LD32(q), q1 = PLM_LD32(q + 4);
			for (r = 0; r < 8; r++, d += dw) {
				PLM_ST32(d, q0);
				PLM_ST32(d + 4, q1);
			}
		}
		else {
			for (r = 0; r < 8; r++, d += dw) {
				for (c = 0; c < 8; c++) {
					d[c] = plm_clamp(d[c] + v[c]);
				}
			}
		}
	}
	else if (rev == 1 && rov == 0) {
		// the first column alone: each row one number, a[0] >> 8 (its
		// rounding in it already)
		for (r = 0; r < 8; r++, d += dw) {
			int value = t[r * 8] >> 8;
			if (intra) {
				uint32_t q = plm_clamp(value) * 0x01010101u;
				PLM_ST32(d, q);
				PLM_ST32(d + 4, q);
			}
			else {
				plm_add_const8(d, value);
			}
		}
	}
	else if (intra) {
		#define PLM_ROW_LOOP(EV, OV) \
			for (r = 0; r < 8; r++, d += dw) { \
				const int *a = t + r * 8; \
				PLM_IDCT_SPLIT(EV, OV, PLM_ROW_IN, PLM_ROW_SET); \
			}
		PLM_IDCT_PAIRS(rev, rov, PLM_ROW_LOOP);
		#undef PLM_ROW_LOOP
	}
	else {
		#define PLM_ROW_LOOP(EV, OV) \
			for (r = 0; r < 8; r++, d += dw) { \
				const int *a = t + r * 8; \
				PLM_IDCT_SPLIT(EV, OV, PLM_ROW_IN, PLM_ROW_ADD); \
			}
		PLM_IDCT_PAIRS(rev, rov, PLM_ROW_LOOP);
		#undef PLM_ROW_LOOP
	}
	#undef PLM_ROW_IN
	#undef PLM_ROW_SET
	#undef PLM_ROW_ADD

	// block_data zeros again: where it had coefficients
	if (self->block_n > 64) {
		memset(self->block_data, 0, sizeof(self->block_data));
	}
	else {
		for (c = 0; c < self->block_n; c++) {
			s[self->block_at[c]] = 0;
		}
	}
	self->block_cols = self->block_rows = 0;
	self->block_n = 0;
}

void plm_video_idct(int *block) {
	int
		b1, b3, b4, b6, b7, tmp1, tmp2, m0,
		x0, x1, x2, x3, x4, y3, y4, y5, y6, y7;

	// Transform columns
	for (int i = 0; i < 8; ++i) {
		b1 = block[4 * 8 + i];
		b3 = block[2 * 8 + i] + block[6 * 8 + i];
		b4 = block[5 * 8 + i] - block[3 * 8 + i];
		tmp1 = block[1 * 8 + i] + block[7 * 8 + i];
		tmp2 = block[3 * 8 + i] + block[5 * 8 + i];
		b6 = block[1 * 8 + i] - block[7 * 8 + i];
		b7 = tmp1 + tmp2;
		m0 = block[0 * 8 + i];
		x4 = ((b6 * 473 - b4 * 196 + 128) >> 8) - b7;
		x0 = x4 - (((tmp1 - tmp2) * 362 + 128) >> 8);
		x1 = m0 - b1;
		x2 = (((block[2 * 8 + i] - block[6 * 8 + i]) * 362 + 128) >> 8) - b3;
		x3 = m0 + b1;
		y3 = x1 + x2;
		y4 = x3 + b3;
		y5 = x1 - x2;
		y6 = x3 - b3;
		y7 = -x0 - ((b4 * 473 + b6 * 196 + 128) >> 8);
		block[0 * 8 + i] = b7 + y4;
		block[1 * 8 + i] = x4 + y3;
		block[2 * 8 + i] = y5 - x0;
		block[3 * 8 + i] = y6 - y7;
		block[4 * 8 + i] = y6 + y7;
		block[5 * 8 + i] = x0 + y5;
		block[6 * 8 + i] = y3 - x4;
		block[7 * 8 + i] = y4 - b7;
	}

	// Transform rows
	for (int i = 0; i < 64; i += 8) {
		b1 = block[4 + i];
		b3 = block[2 + i] + block[6 + i];
		b4 = block[5 + i] - block[3 + i];
		tmp1 = block[1 + i] + block[7 + i];
		tmp2 = block[3 + i] + block[5 + i];
		b6 = block[1 + i] - block[7 + i];
		b7 = tmp1 + tmp2;
		m0 = block[0 + i];
		x4 = ((b6 * 473 - b4 * 196 + 128) >> 8) - b7;
		x0 = x4 - (((tmp1 - tmp2) * 362 + 128) >> 8);
		x1 = m0 - b1;
		x2 = (((block[2 + i] - block[6 + i]) * 362 + 128) >> 8) - b3;
		x3 = m0 + b1;
		y3 = x1 + x2;
		y4 = x3 + b3;
		y5 = x1 - x2;
		y6 = x3 - b3;
		y7 = -x0 - ((b4 * 473 + b6 * 196 + 128) >> 8);
		block[0 + i] = (b7 + y4 + 128) >> 8;
		block[1 + i] = (x4 + y3 + 128) >> 8;
		block[2 + i] = (y5 - x0 + 128) >> 8;
		block[3 + i] = (y6 - y7 + 128) >> 8;
		block[4 + i] = (y6 + y7 + 128) >> 8;
		block[5 + i] = (x0 + y5 + 128) >> 8;
		block[6 + i] = (y3 - x4 + 128) >> 8;
		block[7 + i] = (y4 - b7 + 128) >> 8;
	}
}

// YCbCr conversion following the BT.601 standard:
// https://infogalactic.com/info/YCbCr#ITU-R_BT.601_conversion

#define PLM_PUT_PIXEL(RI, GI, BI, Y_OFFSET, DEST_OFFSET) \
	y = ((frame->y.data[y_index + Y_OFFSET]-16) * 76309) >> 16; \
	dest[d_index + DEST_OFFSET + RI] = plm_clamp(y + r); \
	dest[d_index + DEST_OFFSET + GI] = plm_clamp(y - g); \
	dest[d_index + DEST_OFFSET + BI] = plm_clamp(y + b);

#define PLM_DEFINE_FRAME_CONVERT_FUNCTION(NAME, BYTES_PER_PIXEL, RI, GI, BI) \
	void NAME(plm_frame_t *frame, uint8_t *dest, int stride) { \
		int cols = frame->width >> 1; \
		int rows = frame->height >> 1; \
		int yw = frame->y.width; \
		int cw = frame->cb.width; \
		for (int row = 0; row < rows; row++) { \
			int c_index = row * cw; \
			int y_index = row * 2 * yw; \
			int d_index = row * 2 * stride; \
			for (int col = 0; col < cols; col++) { \
				int y; \
				int cr = frame->cr.data[c_index] - 128; \
				int cb = frame->cb.data[c_index] - 128; \
				int r = (cr * 104597) >> 16; \
				int g = (cb * 25674 + cr * 53278) >> 16; \
				int b = (cb * 132201) >> 16; \
				PLM_PUT_PIXEL(RI, GI, BI, 0,      0); \
				PLM_PUT_PIXEL(RI, GI, BI, 1,      BYTES_PER_PIXEL); \
				PLM_PUT_PIXEL(RI, GI, BI, yw,     stride); \
				PLM_PUT_PIXEL(RI, GI, BI, yw + 1, stride + BYTES_PER_PIXEL); \
				c_index += 1; \
				y_index += 2; \
				d_index += 2 * BYTES_PER_PIXEL; \
			} \
		} \
	}

PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_rgb,  3, 0, 1, 2)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_bgr,  3, 2, 1, 0)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_rgba, 4, 0, 1, 2)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_bgra, 4, 2, 1, 0)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_argb, 4, 1, 2, 3)
PLM_DEFINE_FRAME_CONVERT_FUNCTION(plm_frame_to_abgr, 4, 3, 2, 1)


#undef PLM_PUT_PIXEL
#undef PLM_DEFINE_FRAME_CONVERT_FUNCTION



#endif // PL_MPEG_IMPLEMENTATION
