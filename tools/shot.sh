#!/bin/bash
# Take a screenshot from the device, downscaled to 800px wide. usage: tools/shot.sh out.png
adb exec-out screencap -p > "$1.full.png"
python3 -c "
from PIL import Image; im=Image.open('$1.full.png').convert('RGB'); im.thumbnail((800,800)); im.save('$1')"
