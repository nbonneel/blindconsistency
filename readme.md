# Blind Video Temporal Consistency
## SIGGRAPH Asia 2015

Nicolas Bonneel - CNRS LIRIS
James Tompkin - Harvard Paulson SEAS
Kalyan Sunkavalli - Adobe
Deqing Sun - NVIDIA
Sylvain Paris - Adobe
Hanspeter Pfister - Harvard Paulson SEAS
---

## Description

This code implements this paper. It takes a processed or stylized video, say by a per-frame image processing operation, plus the original input video, and attempts to create a temporally-consistent version of that video.

[Project webpage](http://liris.cnrs.fr/~nbonneel/consistency/)

Video (click through):
[![Project video](http://img.youtube.com/vi/reiT2SJh96U/0.jpg)](http://www.youtube.com/watch?v=reiT2SJh96U)

## Compilation

Tested to compile in Visual Studio 2013 or greater.
Depends on FFmpeg, PatchMatch, and CImg.

## Dependencies - included

[FFmpeg](http://ffmpeg.org/)
[FFmpeg Windows builds](http://ffmpeg.zeranoe.com/builds/)
[Barnes et al., PatchMatch, SIGGRAPH 2009](http://gfx.cs.princeton.edu/pubs/Barnes_2009_PAR/index.php)
[CImg](http://www.cimg.eu/)

## Licence

No commercial licence is given whatsoever. This code is distributed purely for education and for research purposes.