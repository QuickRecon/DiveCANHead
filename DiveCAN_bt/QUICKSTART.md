# Quick Start Guide

## Testing the Implementation

Since this is a browser-based application using ES6 modules, you need to serve it over HTTP/HTTPS.

### Option 1: Python HTTP Server (Quick Testing)

```bash
cd DiveCAN_bt
python3 -m http.server 8000
```

Then open: http://localhost:8000/examples/test-ui.html

**Note**: Web Bluetooth API works on localhost HTTP, but production requires HTTPS.

### Option 2: Using Node.js http-server

```bash
# Install http-server globally (one time)
npm install -g http-server

# Run server
cd DiveCAN_bt
http-server -p 8000
```

Then open: http://localhost:8000/examples/test-ui.html

### Option 3: VS Code Live Server Extension

1. Install "Live Server" extension in VS Code
2. Open `DiveCAN_bt/examples/test-ui.html`
3. Click "Go Live" button in bottom right

## Using the Test UI

### 1. Browser Requirements

- Use Chrome, Edge, or Opera (Web Bluetooth supported)
- Firefox and Safari will NOT work

### 2. Connection Steps

1. Click **"Scan & Connect"** button
2. Browser will show device picker - select your Petrel 3
3. Wait for "Protocol stack connected" message
4. Status indicator should turn green

### 3. Reading Data

Try these operations:

- **Read FW Version**: Click to get firmware version string
- **Read HW Version**: Click to get hardware version number
- **Read Configuration**: Click to get 4-byte config block

### 4. Switching Sessions

Before writing data, switch to extended session:

1. Click **"Extended Diagnostic (0x03)"**
2. Wait for success message
3. Session indicator updates to "EXTENDED"

### 5. Raw Commands

Test custom DIDs:

1. Enter DID in hex (e.g., `F000` for firmware version)
2. Click **"Send Read"**
3. Response appears in Response box

### 6. Memory Upload

Test memory transfer:

1. Click **"Upload Memory (128 bytes)"**
2. Watch progress bar
3. Data appears in hex format when complete

### 7. Monitoring

Watch the **Packet Log** section:
- Green = TX (transmitted frames)
- Blue = RX (received frames)
- Red = Errors

## Troubleshooting

### "Web Bluetooth not available"
→ Use Chrome, Edge, or Opera browser

### No devices found
→ Ensure Petrel 3 is on and Bluetooth enabled

### Connection fails
→ Try these steps:
1. Power cycle Petrel 3
2. Refresh browser page
3. Clear browser cache
4. Move closer to device

### "Request timeout"
→ Device not responding:
1. Check DiveCAN device is connected to Petrel
2. Verify Petrel firmware supports BLE bridge
3. Try disconnect/reconnect

### Negative Response Errors

**NRC 0x22 (Conditions Not Correct)**
→ Wrong session - switch to Extended or Programming session

**NRC 0x13 (Incorrect Message Length)**
→ Data format issue - check DID parameters

**NRC 0x31 (Request Out of Range)**
→ Invalid DID or memory address

## Next Steps

### Integrate into Your App

```javascript
import { DiveCANProtocolStack } from './src/index.js';

const stack = new DiveCANProtocolStack();
await stack.connect(device);
const version = await stack.readFirmwareVersion();
console.log(version);
```

### Explore the API

See [README.md](README.md) for full API documentation.

### Build Your UI

Copy `examples/test-ui.html` as a starting point for your custom interface.

## Development Tips

### Enable Debug Logging

Add to test-ui.html:

```javascript
import { Logger } from '../src/utils/Logger.js';
Logger.prototype.setLevel('debug');
```

### Monitor Network Traffic

Open Chrome DevTools:
1. F12 → Console tab
2. Watch for detailed logs
3. Network tab shows Web Bluetooth activity

### Inspect Packets

All TX/RX packets appear in the Packet Log with timestamps and hex data.

## Common Use Cases

### Read All Settings

```javascript
// Get setting count
const countData = await stack.uds.readDataByIdentifier(0x9100);
const count = countData[0];

// Read each setting
for (let i = 0; i < count; i++) {
  const info = await stack.uds.readDataByIdentifier(0x9110 + i);
  const value = await stack.uds.readDataByIdentifier(0x9130 + i);
  console.log(`Setting ${i}:`, info, value);
}
```

### Write Configuration

```javascript
// Must be in extended or programming session
await stack.uds.startSession(0x03);

// Write new config
const newConfig = new Uint8Array([0x01, 0x02, 0x03, 0x04]);
await stack.writeConfiguration(newConfig);

// Verify
const readBack = await stack.readConfiguration();
console.log('Verified:', readBack);
```

### Upload Firmware

```javascript
// Switch to programming session
await stack.uds.startSession(0x02);

// Upload memory region
const memory = await stack.uploadMemory(
  0xC2000080,  // Address
  4096,        // Length
  (current, total) => {
    console.log(`Progress: ${Math.round(current/total*100)}%`);
  }
);

// Save to file or process
console.log('Uploaded:', memory);
```

## Support

For issues, check:
1. Browser console for errors
2. Packet log for protocol issues
3. README.md for detailed documentation

Open an issue on GitHub if problems persist.
