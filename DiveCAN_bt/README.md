# DiveCAN Protocol Stack for Browser

A browser-based JavaScript API for communicating with DiveCAN-compatible devices via Bluetooth Low Energy (BLE). Implements the full protocol stack: BLE → SLIP → DiveCAN → ISO-TP → UDS.

## Features

- **Full Protocol Stack**: Complete implementation of BLE-SLIP-ISO-TP-UDS layers
- **Web Bluetooth API**: Native browser support (Chrome, Edge, Opera)
- **Device Discovery**: Scan and connect to Petrel 3 Bluetooth bridge
- **UDS Diagnostics**: Session control, read/write data identifiers, memory transfer
- **Test UI**: Comprehensive web-based testing interface
- **Modular Architecture**: Clean layer separation for easy testing and extension
- **Event-Driven**: Real-time packet inspection and progress tracking

## Browser Support

- ✅ Chrome 56+
- ✅ Edge 79+
- ✅ Opera 43+
- ❌ Firefox (no Web Bluetooth support)
- ❌ Safari (no Web Bluetooth support)

**Requirements:**
- HTTPS (required for Web Bluetooth API)
- User gesture to initiate connection (button click)

## Quick Start

### 1. Start a local HTTPS server

Since Web Bluetooth requires HTTPS, you need to serve the files over HTTPS:

```bash
cd DiveCAN_bt
npm run dev
```

This starts a simple HTTP server on port 8000. For HTTPS, you'll need a local certificate or use a service like ngrok.

### 2. Open the test UI

Navigate to:
```
http://localhost:8000/examples/test-ui.html
```

### 3. Connect to your Petrel 3

1. Click "Scan & Connect"
2. Select your Petrel 3 from the browser's device picker
3. Wait for connection to establish

### 4. Test UDS operations

- Switch diagnostic sessions
- Read firmware version
- Read/write configuration
- Upload memory blocks

## API Usage

### Basic Example

```javascript
import { DiveCANProtocolStack, DeviceManager } from './src/index.js';

// Create device manager
const deviceManager = new DeviceManager();

// Scan for devices
const devices = await deviceManager.scan();
const petrel = devices[0].device;

// Create protocol stack
const stack = new DiveCANProtocolStack();

// Connect
await stack.connect(petrel);

// Read firmware version
const version = await stack.readFirmwareVersion();
console.log('Firmware:', version);

// Disconnect
await stack.disconnect();
```

### Advanced Usage

```javascript
import {
  DiveCANProtocolStack,
  UDSConstants
} from './src/index.js';

const stack = new DiveCANProtocolStack();

// Set up event handlers
stack.on('connected', () => console.log('Connected'));
stack.on('data', ({ layer, data }) => console.log(`RX [${layer}]:`, data));
stack.on('error', (error) => console.error('Error:', error));

// Connect to device
await stack.connect(device);

// Start extended diagnostic session
await stack.uds.startSession(UDSConstants.SESSION_EXTENDED_DIAGNOSTIC);

// Read configuration
const config = await stack.uds.readDataByIdentifier(UDSConstants.DID_CONFIGURATION_BLOCK);
console.log('Config:', config);

// Write configuration
await stack.uds.writeDataByIdentifier(UDSConstants.DID_CONFIGURATION_BLOCK, newConfig);

// Upload memory with progress tracking
const memory = await stack.uploadMemory(
  UDSConstants.MEMORY_CONFIG,
  128,
  (current, total) => {
    console.log(`Progress: ${current}/${total} bytes`);
  }
);
```

### Direct Layer Access

```javascript
// Access individual protocol layers
const ble = stack.ble;           // BLEConnection
const slip = stack.slip;         // SLIPCodec
const divecan = stack.divecan;   // DiveCANFramer
const isotp = stack.isotp;       // ISOTPTransport
const uds = stack.uds;           // UDSClient

// Direct UDS operations
await uds.startSession(0x03);
const data = await uds.readDataByIdentifier(0xF000);
await uds.writeDataByIdentifier(0xF100, [0x01, 0x02, 0x03, 0x04]);
```

## Architecture

### Protocol Layers

```
┌─────────────────────────────────────────────┐
│           Application Layer                  │
│     (UI, Device Manager, Test Harness)       │
└─────────────────────────────────────────────┘
                     ↕
┌─────────────────────────────────────────────┐
│           UDS Service Layer                  │
│  (Session Control, Read/Write, Upload/Down)  │
└─────────────────────────────────────────────┘
                     ↕
┌─────────────────────────────────────────────┐
│         ISO-TP Transport Layer               │
│    (SF/FF/CF/FC Framing, Reassembly)        │
└─────────────────────────────────────────────┘
                     ↕
┌─────────────────────────────────────────────┐
│         DiveCAN Datagram Layer               │
│     (Source/Target Addressing, Framing)      │
└─────────────────────────────────────────────┘
                     ↕
┌─────────────────────────────────────────────┐
│           SLIP Encoding Layer                │
│      (Encoding/Decoding, Escaping)           │
└─────────────────────────────────────────────┘
                     ↕
┌─────────────────────────────────────────────┐
│         BLE Communication Layer              │
│   (Web Bluetooth API, Connection Mgmt)       │
└─────────────────────────────────────────────┘
```

### File Structure

```
DiveCAN_bt/
├── src/
│   ├── ble/
│   │   └── BLEConnection.js         # Web Bluetooth interface
│   ├── slip/
│   │   └── SLIPCodec.js             # SLIP encoding/decoding
│   ├── divecan/
│   │   ├── DiveCANFramer.js         # DiveCAN datagram layer
│   │   └── constants.js
│   ├── isotp/
│   │   ├── ISOTPTransport.js        # ISO-TP transport layer
│   │   ├── ISOTPStateMachine.js     # State machine
│   │   └── constants.js
│   ├── uds/
│   │   ├── UDSClient.js             # UDS service client
│   │   └── constants.js
│   ├── errors/
│   │   └── ProtocolErrors.js        # Error classes
│   ├── utils/
│   │   ├── ByteUtils.js             # Byte array utilities
│   │   ├── Logger.js                # Logging
│   │   └── Timeout.js               # Timeout management
│   ├── DiveCANProtocolStack.js      # Main coordinator
│   ├── DeviceManager.js             # Device discovery
│   └── index.js                     # Public API
├── examples/
│   └── test-ui.html                 # Test interface
├── package.json
└── README.md
```

## Protocol Details

### BLE Layer
- **Service UUID**: `fe25c237-0ece-443c-b0aa-e02033e7029d`
- **Characteristic UUID**: `27b7570b-359e-45a3-91bb-cf7e70049bd2`
- **Packet Header**: `[0x01, 0x00]` prepended to all packets
- **MTU**: 20 bytes (default)

### SLIP Layer
- **RFC 1055** encoding
- **END byte**: `0xC0`
- **ESC byte**: `0xDB`
- Escape sequences: `0xC0 → 0xDB 0xDC`, `0xDB → 0xDB 0xDD`

### DiveCAN Layer
- **Datagram format**: `[source, target, len_low, len_high, payload...]`
- **Source**: `0xFF` (bluetooth client)
- **Target**: `0x80` (controller/Petrel bluetooth side of bridge)

### ISO-TP Layer
- **ISO 15765-2** with extended addressing
- **Single Frame (SF)**: ≤6 bytes, `[TA, 0x0N, payload...]`
- **First Frame (FF)**: >6 bytes, `[TA, 0x1N, len_low, first 5 bytes...]`
- **Consecutive Frame (CF)**: `[TA, 0x2N, 6 bytes...]`
- **Flow Control (FC)**: `[TA, 0x30, BS, STmin]`
- **Timeouts**: N_Bs=1000ms, N_Cr=1000ms

### UDS Layer
- **ISO 14229-1** diagnostic services
- **Service 0x10**: DiagnosticSessionControl
- **Service 0x22**: ReadDataByIdentifier
- **Service 0x2E**: WriteDataByIdentifier
- **Service 0x34/0x35**: RequestDownload/Upload
- **Service 0x36**: TransferData
- **Service 0x37**: RequestTransferExit

### Data Identifiers (DIDs)
- **0xF000**: Firmware version (string)
- **0xF001**: Hardware version (byte)
- **0xF100**: Configuration block (4 bytes)
- **0x9100**: Setting count
- **0x9110+i**: Setting info for setting i
- **0x9130+i**: Setting value for setting i
- **0x9350**: Save settings to flash

## Events

The `DiveCANProtocolStack` emits the following events:

- **`connected`**: Connection established
- **`disconnected`**: Connection closed
- **`data`**: Data received (with layer info)
- **`frame`**: Frame sent (with layer info)
- **`error`**: Protocol error occurred
- **`progress`**: Progress update (current, total, percent)
- **`udsResponse`**: UDS positive response
- **`udsNegativeResponse`**: UDS negative response (with NRC)

## Error Handling

```javascript
try {
  const version = await stack.readFirmwareVersion();
} catch (error) {
  if (error instanceof UDSError) {
    console.error('UDS Error:', error.getNRCDescription());
    console.error('NRC Code:', error.nrc);
  } else if (error instanceof ISOTPError) {
    console.error('ISO-TP Error:', error.message);
  } else if (error instanceof BLEError) {
    console.error('BLE Error:', error.message);
  }

  // Full error chain
  console.error('Full error:', error.getFullError());
}
```

## Development

### Running the Test UI

```bash
# Start HTTP server
npm run dev

# Open browser to http://localhost:8000/examples/test-ui.html
```

### Debugging

Set logger level in your code:

```javascript
import { Logger } from './src/utils/Logger.js';

// Set global log level
Logger.prototype.setLevel('debug');

// Or per-layer
stack.ble.logger.setLevel('debug');
stack.isotp.logger.setLevel('debug');
stack.uds.logger.setLevel('debug');
```

## Limitations

- **Web Bluetooth API**: Only available in Chromium-based browsers
- **HTTPS Required**: Web Bluetooth API requires secure context
- **User Gesture**: Initial connection must be triggered by user action
- **MTU**: Limited to 20 bytes by default (can be negotiated higher)
- **No Background**: Connection drops when tab becomes inactive

## Troubleshooting

### "Web Bluetooth not available"
- Use Chrome, Edge, or Opera browser
- Ensure you're on HTTPS (or localhost)

### "Connection failed"
- Ensure Petrel 3 is powered on and advertising
- Check Bluetooth is enabled on your computer
- Try moving closer to the device

### "Request timeout"
- Device may not be responding
- Check DiveCAN device is functioning
- Try disconnecting and reconnecting

### "Negative response" errors
- **NRC 0x22**: Wrong session - switch to extended/programming session
- **NRC 0x13**: Incorrect message length - check data format
- **NRC 0x31**: Request out of range - invalid DID or address

## Future Enhancements

- Unit tests (Jest)
- TypeScript definitions
- Settings API (read/write via DIDs 0x9100+)
- Firmware download capability
- Log file retrieval
- Connection state persistence
- Multiple device management
- MTU negotiation
- Bundling (Rollup/Webpack)

## License

MIT

## Contributing

Contributions welcome! Please open an issue or pull request on GitHub.

## References

- **ISO 15765-2**: Road vehicles — Diagnostic communication over Controller Area Network (ISO-TP)
- **ISO 14229-1**: Road vehicles — Unified diagnostic services (UDS)
- **RFC 1055**: Serial Line Internet Protocol (SLIP)
- **Web Bluetooth API**: https://webbluetoothcg.github.io/web-bluetooth/

## Support

For issues and questions, please open an issue on GitHub.
