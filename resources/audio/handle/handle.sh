#!/bin/bash

for file in *.ogg; do
    if [[ -f "$file" ]]; then
        base="${file%.ogg}"
        echo "Processing: $file"
        ffmpeg -y \
            -i "$file" \
            -f lavfi -t 1.5 -i anullsrc=r=44100:cl=mono \
            -filter_complex "[0:a][1:a]concat=n=2:v=0:a=1" \
            -c:a libvorbis -b:a 96k \
            "${base}_padded.ogg"
    fi
done

echo "✅ Done!"
