# Assert that when we send an ID message onto the bus, we get the correct messages back from the board indicating a response to the ping

def test_CAN_PingIDResponse(DiveCANClient, ShimHost):
    DiveCANClient.SendId()
    message = DiveCANClient.ListenForID()
    assert message.arbitration_id == 0xD000004

def test_CAN_PingStatusResponse(DiveCANClient, ShimHost):
    DiveCANClient.SendId()
    message = DiveCANClient.ListenForStatus()
    assert message.arbitration_id == 0xDCB0004

def test_CAN_PingNameResponse(DiveCANClient, ShimHost):
    DiveCANClient.SendId()
    message = DiveCANClient.ListenForName()
    assert message.data.decode("utf-8") == "Rev2Ctl\x00"