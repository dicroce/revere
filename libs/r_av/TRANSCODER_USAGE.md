# r_transcoder Usage Guide

The `r_transcoder` class provides a threaded video transcoder that handles decoding, resizing, encoding, and streaming to a network URL (like Cloudflare Stream Live).

## Features

- **Threaded operation**: Transcoding happens on a background thread
- **Input queue management**: Automatic backpressure when queue is full
- **Adaptive bitrate**: Automatically adjusts bitrate based on actual output
- **Scaling**: Resizes video to target resolution during transcode

## Basic Usage

```cpp
#include "r_av/r_transcoder.h"
#include "r_av/r_demuxer.h"

using namespace r_av;

// 1. Setup source (e.g., from a demuxer or RTSP stream)
r_demuxer demuxer("rtsp://camera/stream", true);
auto video_stream_index = demuxer.get_video_stream_index();
auto vsi = demuxer.get_stream_info(video_stream_index);
auto extradata = demuxer.get_extradata(video_stream_index);

// 2. Create transcoder
r_transcoder transcoder(
    "srt://live-push.cloudflare.com/live/<STREAM_KEY>",  // output_url
    "mpegts",                                             // output_format
    vsi.codec_id,                                         // input_codec
    extradata,                                            // input_extradata
    1280, 720,                                            // output resolution
    {30, 1},                                              // framerate (30 fps)
    2000000,                                              // initial_bitrate (2 Mbps)
    3000000,                                              // max_bitrate (3 Mbps)
    500000                                                // min_bitrate (500 Kbps)
);

// 3. Start transcoding
transcoder.start();

// 4. Feed frames from source (as fast as possible)
// write_frame() will block if queue is full (natural backpressure)
while (running && demuxer.read_frame())
{
    auto fi = demuxer.get_frame_info();
    if (fi.index == video_stream_index)
    {
        transcoder.write_frame(fi.data, fi.size, fi.pts);
    }
}

// 5. Stop transcoding (flushes and finalizes)
transcoder.stop();
```

## Constructor Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `output_url` | string | SRT/RTMP/file URL for output stream |
| `output_format` | string | Format name (e.g., "mpegts" for SRT, "flv" for RTMP) |
| `input_codec` | AVCodecID | Codec ID from input stream |
| `input_extradata` | vector<uint8_t> | SPS/PPS data from input stream |
| `output_width` | uint16_t | Target output width |
| `output_height` | uint16_t | Target output height |
| `framerate` | AVRational | Output framerate (e.g., {30, 1} for 30fps) |
| `initial_bitrate` | uint32_t | Starting bitrate in bits/sec |
| `max_bitrate` | uint32_t | Maximum allowed bitrate (usually source bitrate) |
| `min_bitrate` | uint32_t | Minimum allowed bitrate (quality floor) |

## Bitrate Adaptation

The transcoder measures actual encoded bitrate every 2 seconds and adjusts:

- **If actual < 90% of target**: Increase by 10% (up to max_bitrate)
- **If actual > 110% of target**: Decrease by 15% (down to min_bitrate)

This ensures the stream adapts to network conditions without disconnecting.

## Queue Management

- **Queue size**: 30 frames (~1 second @ 30fps)
- **Backpressure**: `write_frame()` blocks if queue is full
- **Usage pattern**: Feed frames as fast as possible; the transcoder will naturally pace via backpressure

## Cloudflare Stream Live Example

```cpp
// Recommended settings for Cloudflare Stream Live
r_transcoder transcoder(
    "srt://live-push.cloudflare.com/live/YOUR_STREAM_KEY",
    "mpegts",                  // Required for SRT
    vsi.codec_id,
    extradata,
    1280, 720,                 // 720p recommended
    {30, 1},                   // 30fps
    1500000,                   // Start at 1.5 Mbps
    2500000,                   // Max 2.5 Mbps (conservative for upload)
    500000                     // Min 500 Kbps
);
```

## Thread Safety

- `write_frame()`: Thread-safe, can be called from any thread
- `start()` / `stop()`: Thread-safe, idempotent
- Transcoding happens on dedicated worker thread
- Automatic cleanup in destructor

## Error Handling

Errors during transcoding are logged via `R_LOG_ERROR()`. The worker thread continues running unless `stop()` is called or an unrecoverable error occurs.
