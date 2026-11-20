# Qt Multimedia for the Nix package (audio and video in notes, voice notes: docs/UI.md "Audio and
# video"): nixpkgs' qtmultimedia rebuilt without what Brulion never uses — and what made it ten
# times the size of the rest of Qt in a closure (docs/PERFORMANCE.md "Audio and video: what Qt
# Multimedia costs"):
#   * no GStreamer backend (gst-plugins-bad alone is 850 MB of closure; the FFmpeg backend plays
#     and records everything here), no spatial audio (Qt Quick 3D), no PipeWire library (sound
#     goes through PulseAudio's API, which PipeWire's pulse server answers on today's desktops),
#     no VA-API (pictures are decoded on the processor: notes' videos are short);
#   * FFmpeg built for exactly the formats the app plays (docs/SYNTAX.md "Audio and video":
#     Obsidian's mp3 webm wav m4a ogg 3gp flac, mp4 webm ogv mov mkv) and records (Opus in Ogg,
#     AAC in MP4/M4A, WAV), with its own decoders plus dav1d for AV1, libopus for Opus — LGPL, no
#     programs, no network, no filters or devices.
{
  lib,
  qt6,
  ffmpeg-headless,
}:

let
  # Every component FFmpeg knows is off (--disable-everything), then the ones below on.
  decoders = [
    # audio
    "aac"
    "aac_latm"
    "mp3"
    "mp3float"
    "mp2"
    "opus"
    "libopus"
    "vorbis"
    "flac"
    "alac"
    "amrnb"
    "amrwb"
    "pcm_s16le"
    "pcm_s16be"
    "pcm_s24le"
    "pcm_s32le"
    "pcm_f32le"
    "pcm_f64le"
    "pcm_u8"
    "pcm_alaw"
    "pcm_mulaw"
    "adpcm_ima_wav"
    "adpcm_ms"
    # video
    "h264"
    "hevc"
    "vp8"
    "vp9"
    "av1"
    "libdav1d"
    "mpeg4"
    "h263"
    "theora"
    "mjpeg"
    "mpeg1video"
    "mpeg2video"
    # (cover pictures in audio files)
    "png"
  ];
  encoders = [
    "libopus"
    "aac"
    "pcm_s16le"
    # (a video recorder Qt sets up next to the audio one: never used, but QMediaRecorder lists it)
    "mjpeg"
  ];
  demuxers = [
    "mov"
    "matroska"
    "ogg"
    "wav"
    "mp3"
    "flac"
    "aac"
    "amr"
    "mpegps"
    "mpegts"
  ];
  muxers = [
    "ogg"
    "opus"
    "ipod"
    "mp4"
    "mov"
    "wav"
    "matroska"
    "webm"
  ];
  parsers = [
    "aac"
    "aac_latm"
    "h264"
    "hevc"
    "vp8"
    "vp9"
    "av1"
    "opus"
    "vorbis"
    "flac"
    "mpegaudio"
    "mpeg4video"
    "mpegvideo"
    "h263"
    "mjpeg"
    "png"
  ];
  bsfs = [
    "aac_adtstoasc"
    "h264_mp4toannexb"
    "hevc_mp4toannexb"
    "vp9_superframe_split"
    "av1_frame_split"
    "extract_extradata"
    "null"
  ];
  enable = kind: names: map (n: "--enable-${kind}=${n}") names;

  ffmpeg =
    (ffmpeg-headless.override {
      ffmpegVariant = "small";
      withHeadlessDeps = false;
      withSmallDeps = false;
      withFullDeps = false;
      withGPL = false;
      withVersion3 = false;
      buildAvcodec = true;
      buildAvformat = true;
      buildAvutil = true;
      buildSwresample = true;
      buildSwscale = true;
      buildAvdevice = false;
      buildAvfilter = false;
      buildFfmpeg = false;
      buildFfprobe = false;
      buildFfplay = false;
      withOpus = true;
      withDav1d = true;
      withZlib = true;
      withHardcodedTables = true;
      withSafeBitstreamReader = true;
      withDocumentation = false;
    }).overrideAttrs
      (old: {
        pname = "ffmpeg-brulion";
        configureFlags =
          old.configureFlags
          ++ [ "--disable-everything" ]
          ++ enable "decoder" decoders
          ++ enable "encoder" encoders
          ++ enable "demuxer" demuxers
          ++ enable "muxer" muxers
          ++ enable "parser" parsers
          ++ enable "bsf" bsfs
          ++ [ "--enable-protocol=file" ];
        # (FFmpeg's own tests need the components --disable-everything left out.)
        doCheck = false;
      });
in
(qt6.qtmultimedia.override { inherit ffmpeg; }).overrideAttrs (old: {
  buildInputs = lib.filter (
    p:
    !(lib.elem (lib.getName p) [
      "pipewire"
      "libva"
    ])
  ) old.buildInputs;
  propagatedBuildInputs = with qt6; [
    qtbase
    qtdeclarative
    qtsvg
    qtshadertools
  ];
  cmakeFlags = old.cmakeFlags ++ [
    "-DFEATURE_gstreamer=OFF"
    "-DFEATURE_spatialaudio=OFF"
    "-DFEATURE_spatialaudio_quick3d=OFF"
    "-DFEATURE_pipewire=OFF"
    "-DFEATURE_vaapi=OFF"
    "-DFEATURE_ffmpeg=ON"
  ];
  passthru = (old.passthru or { }) // {
    inherit ffmpeg;
  };
})
