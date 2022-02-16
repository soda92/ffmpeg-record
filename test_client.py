import os
import shutil

os.add_dll_directory("D:\\boost_1_78_0\\stage\\lib")

shutil.copy("build/video_lib.dll", "./video_lib.pyd")

import json

data = None
with open("config.json", mode="r", encoding="utf-8") as f:
    data = json.load(f)

url = data["url"]
dir = data["dir"]
name_start = data["name-start"]
name_middle = data["name-middle"]
import video_lib

video_lib.Video_StartRecord(1, url, dir, name_start, name_middle, 0)
import time

time.sleep(2 * 60)
video_lib.Video_StopRecord(1)
