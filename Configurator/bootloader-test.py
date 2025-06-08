
from DiveCANpy import configuration, DiveCAN, bootloader
import threading
import time

firmware_file_path ="/home/aren/DiveCANHeadRev2/STM32/build/STM32.bin"

firmware_chunksize = 8  # Bytes per chunk for firmware update

if firmware_file_path == "None" or firmware_file_path == None:
    print("No firmware file selected")
    exit()

# Connect to the bootloader
bootloader_client = bootloader.STMBootloader("/dev/ttyCAN0")
# Once to go to the bootloader, twice to stay there
bootloader_client.send_bootloader()
bootloader_client.send_bootloader() 

bootloader_client.get_bootloader_info()

bootloader_client.send_erase_memory()

with open(firmware_file_path, 'rb') as firmware_file:
    file_bytes = firmware_file.read()

    # Write the firmware to the device
    for i in range(0, len(file_bytes), firmware_chunksize):
        chunk = file_bytes[i:i+firmware_chunksize]
        state_str = f"State: Writing firmware {i + len(chunk)}/{len(file_bytes)}"
        print(state_str)
        try:
            bootloader_client.send_write_memory(0x08000000 + i, len(chunk), chunk)
        except bootloader.STMBootloaderNoMessageException as e:
            print(f"Error writing firmware at address {0x08000000 + i}: {e}, retrying...")
            bootloader_client.send_bootloader()
            bootloader_client.send_bootloader()
            bootloader_client.send_bootloader()
            bootloader_client.send_bootloader()
            bootloader_client.send_bootloader()
            bootloader_client.send_write_memory(0x08000000 + i, len(chunk), chunk)
            
        time.sleep(1)  # Small delay to try and make things reliable
    
    # Read the firmware back to verify
    for i in range(0, len(file_bytes), firmware_chunksize):
        actual_chunk = file_bytes[i:i+firmware_chunksize]
        read_chunk = bootloader_client.send_read_memory(0x08000000 + i, len(actual_chunk))
        state_str = f"State: Verifying firmware {i + len(actual_chunk)}/{len(file_bytes)}"
        print(state_str)
        
        for j in range(len(actual_chunk)):
            if read_chunk[j] != actual_chunk[j]:
                print(f"Error verifying firmware at address {0x08000000 + i}")
                exit()

    bootloader_client.send_go()
try:
    print("Firmware written successfully")
except Exception as e:
    print(f"Error writing firmware: {e}")