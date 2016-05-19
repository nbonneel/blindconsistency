REM usage: stabilize.exe input_video per_frame_processed_video temporal_weight output_file max_frames width height
REM input_video, per_frame_processed_video and output_file can be image sequences (put the first file ; files should be numbered), MPEG/AVI files (or anything supported by ffmpeg), or YUV files
REM temporal weight around 1.0
REM max_frames is the  max number of frames to process (use any large number to process the whole video)
REM width and height are optional for images/mpeg files. For YUV files, mandatory and indicates the frame width/height.
REM for best quality, export in YUV and /then/ use ffmpeg to compress in mp4 ; the mp4 our tool produce may not even export well to Premiere or other softwares.

REM example:
REM stabilize.exe old_man.mp4 old_man_autocolors.avi 1.0 old_man_regularized_000.png 10000

stabilize.exe old_man.mp4 old_man_autocolors.avi 1.0 old_man_regularized.mp4 10000