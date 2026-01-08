#!/bin/bash
# Build and generate UF2 for ESP32-S3

set -e

echo "Building project..."
idf.py build

echo "Generating UF2..."
esptool.py --chip esp32s3 merge_bin \
  -o build/keyer_c.uf2 \
  -f uf2 \
  -fm dio \
  -ff 80m \
  -fs 16MB \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x21000 build/ota_data_initial.bin \
  0x40000 build/keyer_c.bin

echo "✓ UF2 file generated: build/keyer_c.uf2"
ls -lh build/keyer_c.uf2
