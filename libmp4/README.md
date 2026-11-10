# libmp4

`libmp4` is a small MP4 reader/writer for embedded cloud-recording clips. It is designed for low-memory Linux devices where `/tmp` may be `tmpfs`, so media payloads are written directly to the target file instead of being accumulated in RAM.

## Features

- H.264 Annex-B video track, written as `avc1/avcC`.
- H.265 Annex-B video track, written as `hvc1/hvcC`.
- G.711 A-law audio track, written as `alaw`.
- G.711 u-law audio track, written as `ulaw`.
- Multiple tracks in one MP4 file.
- MP4 reader for non-fragmented `moov` sample tables.
- Bounded reader sample-table allocation for untrusted MP4 input.
- Video readback converts MP4 length-prefixed NALs back to Annex-B.
- `ftyp + mdat + moov` layout.
- One chunk per sample to keep indexing simple.
- Optional file-size limit through `max_file_bytes`; `0` means unlimited.

This is intentionally smaller than a general MP4 muxer. It targets short clips such as 5 fps, 30 second cloud-storage files. It does not handle B-frame composition offsets, fragmented MP4, AAC, seeking while recording, or long-recording index optimization.

## Build

```sh
make -C libmp4
```

Output:

```text
libmp4/libmp4.a
```

Include path:

```text
libmp4/include
```

Source layout:

```text
source/libmp4.c          common version/error helpers
source/libmp4_writer.c   MP4 writer state, boxes, sample tables
source/libmp4_reader.c   MP4 reader state, boxes, sample tables
source/libmp4_h264.c     H.264 NAL scan, avcC write/read
source/libmp4_h265.c     H.265 NAL scan, hvcC write/read
source/libmp4_g711.c     G.711 duration, stsd write/read
```

## Memory And IO Model

The write cache is bounded below 10 KB:

```c
#define LIBMP4_WRITE_CACHE_DEFAULT 8192u
#define LIBMP4_WRITE_CACHE_MAX 8192u
```

If `cfg.write_cache_size` is larger than `LIBMP4_WRITE_CACHE_MAX`, it is clamped to 8192 bytes.

Frame payloads are not buffered as whole files or GOPs. Video samples are converted from Annex-B start-code NALs to MP4 length-prefixed NALs and written immediately. G.711 samples are written immediately.

The library keeps per-track sample metadata until `libmp4_close()` because normal MP4 stores indexes in `moov` at the end. For 5 fps and 30 seconds, one video track needs about 150 sample records. An 8 kHz mono G.711 track with one 20 ms packet per sample needs about 1500 sample records for the same clip. `max_samples` is a per-track limit, so choose the video and audio limits separately.

The reader parses MP4 sample tables into per-track metadata, then seeks directly to each requested sample. It does not read the whole file into memory. By default it accepts at most 4 input tracks and 2048 samples per track. The same per-track limit bounds `stts`, `stsc`, `stco/co64`, and `stss` entries before allocation.

Recommended low-end settings:

```c
cfg.write_cache_size = 8192;
cfg.max_tracks = 2;
cfg.max_samples = 1600;     /* default per-track cap; override video below */
cfg.max_file_bytes = 0;     /* no fixed file-size cap */
```

## Multi-Track API

Open the file with `width` and `height` left as `0`, then add the tracks explicitly.

Track add functions return a track handle. A value `>= 0` is valid and must be passed back to the write function. A negative value is a `LIBMP4_ERR_*` code.

```c
#include "libmp4.h"

#include <string.h>

int save_h264_g711a_clip(const char* path)
{
    libmp4_config_t cfg;
    libmp4_writer_t* mp4;
    int video_track;
    int audio_track;
    int ret;

    memset(&cfg, 0, sizeof(cfg));
    cfg.path = path;
    cfg.write_cache_size = 8192;
    cfg.max_tracks = 2;
    cfg.max_samples = 1600; /* default per-track cap; audio needs about 1500 */
    cfg.max_file_bytes = 0;

    mp4 = libmp4_open(&cfg);
    if (mp4 == 0) {
        return LIBMP4_ERR_IO;
    }

    video_track = libmp4_add_h264_track(mp4, 640, 360, 5, 1, 160);
    audio_track = libmp4_add_g711a_track(mp4, 8000, 1, 1600);
    if (video_track < 0 || audio_track < 0) {
        libmp4_abort(mp4);
        return video_track < 0 ? video_track : audio_track;
    }

    /*
     * Feed one encoded H.264 Annex-B frame at a time.
     * The first key frame must include SPS/PPS.
     */
    ret = libmp4_write_h264_annexb_track(mp4,
                                         video_track,
                                         video_frame,
                                         video_frame_size,
                                         200, /* 5 fps = 200 ms */
                                         keyframe ? LIBMP4_FLAG_KEYFRAME : 0);
    if (ret != LIBMP4_OK) {
        libmp4_abort(mp4);
        return ret;
    }

    /*
     * Feed one G.711 packet at a time.
     * For 8 kHz mono, 160 bytes is normally 20 ms.
     * If duration_ms is 0, libmp4 derives duration from bytes/rate/channels.
     */
    ret = libmp4_write_g711_track(mp4,
                                  audio_track,
                                  g711_data,
                                  g711_size,
                                  20);
    if (ret != LIBMP4_OK) {
        libmp4_abort(mp4);
        return ret;
    }

    return libmp4_close(mp4);
}
```

## H.265 Example

```c
libmp4_config_t cfg;
libmp4_writer_t* mp4;
int video_track;

memset(&cfg, 0, sizeof(cfg));
cfg.path = "clip_h265.mp4";
cfg.write_cache_size = 8192;
cfg.max_tracks = 1;
cfg.max_samples = 160;
cfg.max_file_bytes = 0;

mp4 = libmp4_open(&cfg);
video_track = libmp4_add_h265_track(mp4, 640, 360, 5, 1, 160);

/* The first key frame must include VPS/SPS/PPS. */
libmp4_write_h265_annexb_track(mp4,
                               video_track,
                               h265_frame,
                               h265_frame_size,
                               200,
                               LIBMP4_FLAG_KEYFRAME);

libmp4_close(mp4);
```

## Legacy Single H.264 API

For old callers, `libmp4_open()` still creates one H.264 track automatically when `cfg.width` and `cfg.height` are non-zero.

```c
memset(&cfg, 0, sizeof(cfg));
cfg.path = "clip_h264.mp4";
cfg.width = 640;
cfg.height = 360;
cfg.fps_num = 5;
cfg.fps_den = 1;
cfg.write_cache_size = 8192;
cfg.max_samples = 160;
cfg.max_file_bytes = 0;

mp4 = libmp4_open(&cfg);
libmp4_write_h264_annexb(mp4, frame_data, frame_size, 200,
                         LIBMP4_FLAG_KEYFRAME);
libmp4_close(mp4);
```

## Reader API

The reader is intended for normal, non-fragmented MP4 files with `moov` sample tables. Files generated by this library are supported.

```c
libmp4_reader_t* reader;
libmp4_reader_config_t reader_cfg;
libmp4_track_info_t track_info;
libmp4_sample_info_t sample_info;
uint8_t* buffer;
size_t bytes;
int track;
int ret;

memset(&reader_cfg, 0, sizeof(reader_cfg));
reader_cfg.max_tracks = 2;
reader_cfg.max_samples = 1600;
reader = libmp4_reader_open_with_config("clip.mp4", &reader_cfg);
track = 0;
ret = libmp4_reader_get_track_info(reader, track, &track_info);

/* Query required output size first. */
ret = libmp4_reader_read_sample(reader,
                                track,
                                0,
                                NULL,
                                0,
                                &bytes,
                                &sample_info,
                                LIBMP4_READ_FLAG_PREPEND_CONFIG);

buffer = malloc(bytes);
ret = libmp4_reader_read_sample(reader,
                                track,
                                0,
                                buffer,
                                bytes,
                                &bytes,
                                &sample_info,
                                LIBMP4_READ_FLAG_PREPEND_CONFIG);

libmp4_reader_close(reader);
```

For H.264/H.265 tracks, `libmp4_reader_read_sample()` returns Annex-B bytes. Use `LIBMP4_READ_FLAG_PREPEND_CONFIG` when the caller wants VPS/SPS/PPS or SPS/PPS before the returned sample. For G.711 tracks, the returned bytes are raw A-law or u-law audio.

`libmp4_reader_open()` remains available and uses the bounded defaults: `LIBMP4_READER_DEFAULT_MAX_TRACKS` and `LIBMP4_READER_DEFAULT_MAX_SAMPLES`. In `libmp4_reader_config_t`, a zero field selects its default; set the values explicitly for a smaller device budget or a longer clip.

## Codec Requirements

H.264 and H.265 input must be Annex-B:

```text
00 00 00 01 <NAL>
00 00 01    <NAL>
```

For H.264, the writer collects SPS/PPS from the stream and writes them into `avcC`. The file cannot be finalized unless at least one SPS and one PPS were seen.

For H.265, the writer collects VPS/SPS/PPS from the stream and writes them into `hvcC`. The file cannot be finalized unless at least one VPS, SPS, and PPS were seen.

Parameter-set NALs and AUD NALs are skipped from media samples after being collected for the MP4 header. Most encoders can be configured to include codec headers before each IDR frame. For short cloud clips, enabling that is recommended so every MP4 has the codec configuration it needs.

G.711 input is raw A-law or u-law bytes. Use `libmp4_add_g711a_track()` for A-law and `libmp4_add_g711u_track()` for u-law.

## tmpfs Guidance

On low-end embedded Linux, `/tmp` is often `tmpfs`, so files written there consume RAM-backed storage. `libmp4` does not impose a fixed file-size cap. If a product later needs one, set `max_file_bytes`; otherwise keep it `0`.

```c
cfg.max_file_bytes = 0;     /* unlimited */
```

Recommended policy for cloud recording:

- Keep only one active file in `/tmp`.
- Write to `clip.mp4.tmp`.
- On success, rename to `clip.mp4` or upload immediately.
- On failure, call `libmp4_abort()` and delete the temp file.
- Do not queue multiple failed clips in `/tmp`.
- If a product later needs a safety cap, make it a configuration value instead of a hard-coded limit.

## Return Codes

```text
LIBMP4_OK          success
LIBMP4_ERR_ARG     invalid argument or closed writer
LIBMP4_ERR_IO      file write/seek/flush error
LIBMP4_ERR_NOMEM   allocation failed
LIBMP4_ERR_FORMAT  no samples or missing video parameter sets at close
LIBMP4_ERR_LIMIT   max tracks/samples, optional file-size cap, or parameter-set size exceeded
```

Use `libmp4_error_text(ret)` for a short diagnostic string.

## Close And Abort

- `libmp4_close()` writes `moov`, patches `mdat`, flushes the cache, closes the file, and frees memory.
- `libmp4_abort()` closes and frees without writing final indexes. Delete the partial file after aborting.
- Call `libmp4_flush()` only when the product explicitly needs data pushed to the file before close.
