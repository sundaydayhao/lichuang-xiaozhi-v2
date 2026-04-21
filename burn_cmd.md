1. 刷 284x240

python -m esptool --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 \
  /home/hao/projects/xiaozhi-esp32-zzx-2/releases/v2.0.5_bread-compact-ml307camtou-284x240_merged.bin

2. 刷 320x240

python -m esptool --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  write_flash 0x0 \
  /home/hao/projects/xiaozhi-esp32-zzx-2/releases/v2.0.5_bread-compact-ml307camtou-320x240_merged.bin

