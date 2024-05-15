import DiveCAN
import HWShim

def resetBoard(divecan_client: DiveCAN.DiveCAN):
    divecan_client.send_bootloader()
    # This should kick us out of the bootloader
    divecan_client.send_id(1)

