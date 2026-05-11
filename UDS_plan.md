# DiveCAN UDS Migration Implementation Plan

## Objective
Migrate the DiveCAN menu handling subsystem (menu.c) to a complete UDS (Unified Diagnostics Standard) implementation while maintaining backward compatibility with existing Shearwater dive computers.

## Current State
- **menu.c** implements a custom protocol (reverse-engineered, incomplete)
- 5 menu items: 1 static firmware version, 4 editable configuration bytes
- Custom opcodes: MENU_REQ (0x04), MENU_ACK_INIT (0x05), MENU_RESP_HEADER (0x10)
- Multi-message stateful handling using static variables
- No existing unit tests for menu system
- Integrates via `RespMenu()` in DiveCAN.c when MENU_ID (0xD0A0000) received

## Target State
- Full UDS (ISO-14229) implementation over ISO-TP (ISO-15765-2) transport
- Services: ReadDataByIdentifier (0x22), WriteDataByIdentifier (0x2E), RequestUpload (0x35), RequestDownload (0x34), TransferData (0x36), TransferExit (0x37)
- Settings DIDs: 0x9100-0x9350 for menu/configuration
- Memory regions: BLOCK1 (config), BLOCK2 (logs), BLOCK3 (MCU ID)
- Backward compatible with existing Petrel/Choptima dive computers
- Separate UDS module with menu.c as compatibility wrapper

## Architecture

### Module Structure
```
STM32/Core/Src/DiveCAN/
├── uds/
│   ├── isotp.c              # ISO-TP transport layer (new)
│   ├── isotp.h
│   ├── uds.c                # UDS service dispatcher (new)
│   ├── uds.h
│   ├── uds_services.c       # Service implementations (new)
│   ├── uds_services.h
│   ├── uds_settings.c       # Settings DID handlers (new)
│   ├── uds_settings.h
│   ├── uds_memory.c         # Memory upload/download (new)
│   └── uds_memory.h
├── menu.c                   # Modified: compatibility wrapper
├── DiveCAN.c                # Modified: route to UDS or legacy
└── Transciever.c            # Modified: add ISO-TP TX helpers

STM32/Tests/
├── isotp_tests.cpp          # ISO-TP unit tests (new)
├── uds_tests.cpp            # UDS services unit tests (new)
└── menu_tests.cpp           # Legacy menu tests (new)

HW Testing/Tests/
└── test_uds.py              # Hardware integration tests (new)
```

### Key Data Structures

**ISO-TP Context** (isotp.h):
```c
typedef struct {
    ISOTPState_t state;          // IDLE, RECEIVING_MULTI, TRANSMITTING_MULTI, WAIT_FLOW_CONTROL
    uint16_t dataLength;         // Total length for multi-frame
    uint16_t bytesReceived;
    uint8_t sequenceNumber;      // Consecutive frame counter
    uint8_t blockSize;           // Flow control block size
    uint8_t separationTime;      // Flow control separation time (ms)
    uint8_t buffer[4095];        // Max UDS message size
    Timestamp_t lastFrameTime;
    DiveCANType_t source;
    DiveCANType_t target;
} ISOTPContext_t;
```

**UDS Session** (uds.h):
```c
typedef struct {
    ISOTPContext_t isotpCtx;
    bool transferActive;         // RequestDownload/Upload active
    uint8_t transferBlockCounter;
    uint32_t transferAddress;
    uint32_t transferLength;
    uint8_t transferDirection;   // 0=download, 1=upload
} UDSSession_t;
```

**Setting Definition** (uds_settings.h):
```c
typedef struct {
    const char *label;
    SettingKind_t kind;          // TEXT, SELECTION, NUMBER
    bool editable;
    uint64_t maxValue;
    uint64_t currentValue;
    const char **options;
    uint8_t optionCount;
} SettingDefinition_t;
```

### Configuration Mapping

Map Configuration_t (32-bit bitfield) to 8 UDS settings:

| Index | DID    | Label        | Field                         | Kind   | Max |
|-------|--------|--------------|-------------------------------|--------|-----|
| 0     | 0x9110 | FW Commit    | COMMIT_HASH (static)          | TEXT   | 1   |
| 1     | 0x9111 | Cell 1 Type  | config.cell1                  | SELECT | 2   |
| 2     | 0x9112 | Cell 2 Type  | config.cell2                  | SELECT | 2   |
| 3     | 0x9113 | Cell 3 Type  | config.cell3                  | SELECT | 2   |
| 4     | 0x9114 | Power Mode   | config.powerMode              | SELECT | 3   |
| 5     | 0x9115 | Cal Method   | config.calibrationMode        | SELECT | 3   |
| 6     | 0x9116 | Voltage Thr  | config.dischargeThresholdMode | SELECT | 1   |
| 7     | 0x9117 | PPO2 Control | config.ppo2controlMode        | SELECT | 2   |

**Backward Compatibility Compromise**: DIDs 0x9130-0x9133 provide raw byte access for old dive computers, while new clients use granular DIDs 0x9131-0x9137.

### Backward Compatibility Strategy

Detection in `RespMenu()`:
```c
void RespMenu(const DiveCANMessage_t *message, const DiveCANDevice_t *deviceSpec,
              Configuration_t *configuration) {
    uint8_t pci = message->data[0] & 0xF0;
    bool isUDS = (pci == 0x00 || pci == 0x10 || pci == 0x20 || pci == 0x30);

    if (isUDS) {
        static UDSSession_t udsSession;
        UDS_ProcessMessage(&udsSession, message, deviceSpec, configuration);
    } else {
        ProcessMenu(message, deviceSpec, configuration);  // Legacy
    }
}
```

**Shearwater Quirk**: Accept Flow Control frames with dst=0xFF (non-standard) in `ISOTP_ProcessRxFrame()`.

## Implementation Phases

### Phase 1: Test Existing Behavior (Week 1)
**Goal**: Establish baseline tests before changes

**Tasks**:
1. Create `STM32/Tests/menu_tests.cpp`
2. Mock sendCANMessage(), getConfigBytes(), setConfigBytes()
3. Write tests for:
   - Menu request sends ACK with item count=5
   - Menu item request returns title
   - Menu flags request returns current value
   - Menu save updates configuration
   - Multi-message save sequence

**Deliverables**:
- menu_tests.cpp with 10+ test cases
- All tests pass with current menu.c
- Add to STM32/Tests/Makefile

**Critical Files**:
- `STM32/Tests/menu_tests.cpp` (new)
- `STM32/Tests/Makefile` (modify to include menu.c)

### Phase 2: ISO-TP Transport Layer (Weeks 2-3)
**Goal**: Implement ISO-TP with comprehensive testing

**Tasks**:
1. Create `STM32/Core/Src/DiveCAN/uds/isotp.c/h`
2. Implement ISO-TP state machine:
   - Single Frame (SF): 1 CAN frame, ≤7 bytes
   - First Frame (FF): Multi-frame start
   - Consecutive Frame (CF): Continuation with sequence number
   - Flow Control (FC): Receiver pacing
3. Handle Shearwater quirk (dst=0xFF in FC)
4. Create `STM32/Tests/isotp_tests.cpp` with tests for:
   - Single frame extraction
   - First frame requests flow control
   - Consecutive frame assembly
   - Wrong sequence number error
   - Multi-frame transmission
   - Shearwater FC quirk acceptance
   - Timeout handling

**Deliverables**:
- isotp.c/h with full state machine
- isotp_tests.cpp with 15+ test cases, 90%+ coverage
- No changes to existing menu.c functionality

**Critical Files**:
- `STM32/Core/Src/DiveCAN/uds/isotp.c` (new)
- `STM32/Core/Src/DiveCAN/uds/isotp.h` (new)
- `STM32/Tests/isotp_tests.cpp` (new)

### Phase 3: UDS Service Dispatcher (Week 4)
**Goal**: UDS message routing and error handling

**Tasks**:
1. Create `STM32/Core/Src/DiveCAN/uds/uds.c/h`
2. Implement `UDS_ProcessMessage()` with ISO-TP integration
3. Implement service dispatcher (switch on SID)
4. Implement `UDS_SendNegativeResponse()` with error codes
5. Implement detection in `DiveCAN.c::RespMenu()`
6. Create stub service handlers (return NRC for now)
7. Write unit tests for:
   - Service routing
   - Negative responses
   - Invalid SID handling
   - ISO-TP integration

**Deliverables**:
- uds.c/h with dispatcher framework
- DiveCAN.c routes UDS vs legacy messages
- uds_tests.cpp with 10+ test cases

**Critical Files**:
- `STM32/Core/Src/DiveCAN/uds/uds.c` (new)
- `STM32/Core/Src/DiveCAN/uds/uds.h` (new)
- `STM32/Core/Src/DiveCAN/DiveCAN.c` (modify RespMenu)
- `STM32/Tests/uds_tests.cpp` (new)

### Phase 4: Settings Implementation (Weeks 5-6)
**Goal**: UDS Settings DIDs (0x9100-0x9350)

**Tasks**:
1. Create `STM32/Core/Src/DiveCAN/uds/uds_settings.c/h`
2. Define SettingDefinition_t array mapping Configuration_t fields
3. Implement `uds_services.c/h`:
   - Service 0x22 ReadDataByIdentifier
   - Service 0x2E WriteDataByIdentifier
4. Implement DID handlers:
   - 0x9100: SettingCount → 8
   - 0x9110+i: SettingInfo → label, kind, editable
   - 0x9130+i: SettingValue → max, current (u64 BE)
   - 0x9150+i+(j<<4): SettingLabel → option labels
   - 0x9350: SettingSave → call saveConfiguration()
5. Write unit tests for:
   - Read setting count
   - Read setting value
   - Write setting value
   - Settings save
   - Invalid DID handling
   - Endianness (u64 BE)
6. Add hardware tests in `HW Testing/Tests/test_uds.py`:
   - Read/write settings via UDS
   - Verify persistence
7. Extend DiveCANpy with ISO-TP helpers

**Deliverables**:
- uds_settings.c/h with full settings implementation
- uds_services.c/h with 0x22/0x2E services
- All 8 settings accessible via UDS
- Unit tests for settings (20+ cases)
- Hardware integration tests (5+ cases)
- DiveCANpy ISO-TP support

**Critical Files**:
- `STM32/Core/Src/DiveCAN/uds/uds_settings.c` (new)
- `STM32/Core/Src/DiveCAN/uds/uds_services.c` (new)
- `STM32/Core/Src/configuration.c` (verify integration)
- `HW Testing/Tests/test_uds.py` (new)

### Phase 5: Memory Upload (Weeks 7-8)
**Goal**: RequestUpload (0x35) for reading flash/logs

**Tasks**:
1. Create `STM32/Core/Src/DiveCAN/uds/uds_memory.c/h`
2. Implement memory region validation:
   - BLOCK1: 0xC2000080-0xC2000FFF → flash 0x00000080 (8-byte aligned)
   - BLOCK2: 0xC3001000-0xC3FFFFFF → flash 0x00010000 (4KB aligned)
   - BLOCK3: 0xC5000000-0xC500007F → MCU ID 0x1FFFF7F0
3. Implement services:
   - 0x35 RequestUpload
   - 0x36 TransferData (upload direction)
   - 0x37 TransferExit
4. Write unit tests for:
   - Region validation
   - Upload request
   - Block transfers
   - Sequence counter
   - Transfer completion
5. Add hardware tests for:
   - Upload flash config
   - Upload MCU ID
   - Block sequence

**Deliverables**:
- uds_memory.c/h with upload support
- Services 0x35, 0x36, 0x37 implemented
- Unit tests (15+ cases)
- Hardware tests (3+ cases)

**Critical Files**:
- `STM32/Core/Src/DiveCAN/uds/uds_memory.c` (new)
- `STM32/Core/Src/Hardware/flash.c` (interface for memory access)

### Phase 6: Firmware Download (Weeks 9-10)
**Goal**: RequestDownload (0x34) for firmware updates

**Tasks**:
1. Implement authentication/unlocking mechanism
2. Implement Service 0x34 RequestDownload
3. Implement Service 0x36 TransferData (download direction)
4. Add flash erase/program logic using HAL_FLASH_*
5. Implement checksum verification in TransferExit
6. Write unit tests for:
   - Download request validation
   - Authentication requirement
   - Flash write operations
   - Checksum verification
7. **CRITICAL**: Test on non-production device first

**Deliverables**:
- Service 0x34 implemented with authentication
- Flash programming working
- Unit tests (10+ cases)
- Hardware tests on dev board only

**Critical Files**:
- `STM32/Core/Src/DiveCAN/uds/uds_memory.c` (modify)
- `STM32/Core/Src/Hardware/flash.c` (flash programming interface)

### Phase 7: Legacy Compatibility Validation (Week 11)
**Goal**: Ensure existing menu.c behavior unchanged

**Tasks**:
1. Run all menu_tests.cpp (Phase 1 tests)
2. Run existing `HW Testing/Tests/test_menu.py`
3. Test with real Petrel 3 hardware if available
4. Verify configuration save/load still works
5. Document protocol detection logic
6. Add regression tests

**Deliverables**:
- All legacy tests pass
- Real hardware validation complete
- Regression test suite
- Documentation of compromises

**Critical Files**:
- `STM32/Core/Src/DiveCAN/menu.c` (verify unchanged)
- `HW Testing/Tests/test_menu.py` (existing tests)

### Phase 8: Documentation & Code Review (Week 12)
**Goal**: Production-ready code

**Tasks**:
1. Write `STM32/UDS_Implementation.md` documenting:
   - Protocol overview
   - Service descriptions
   - DID mapping
   - Memory regions
   - Backward compatibility notes
2. Add Doxygen comments to all public functions
3. Update CLAUDE.md with UDS architecture
4. Code review
5. Performance testing (CAN bus timing)
6. Update user documentation

**Deliverables**:
- UDS_Implementation.md complete
- Code review approved
- CLAUDE.md updated
- Performance validated

## Testing Strategy

### Unit Tests (CppUTest)
- **ISO-TP Layer**: 15+ tests covering all state transitions, multi-frame, quirks
- **UDS Services**: 20+ tests covering all services, DIDs, error codes
- **Settings**: 20+ tests for read/write, validation, persistence
- **Memory**: 15+ tests for region validation, transfers
- **Legacy Menu**: 10+ tests preserving existing behavior

**Target**: 85%+ code coverage on all new modules

### Hardware Integration Tests (pytest)
- **Settings**: Read/write via UDS, verify with DiveCANpy
- **Memory Upload**: Transfer blocks, verify data
- **Legacy Menu**: Ensure old protocol still works
- **Multi-frame**: Test ISO-TP with real CAN hardware

**Infrastructure**: Existing test stand with Arduino Due HWShim, CAN adapter, PSU

### Manual Testing
- Test with Petrel 3 dive computer (if available)
- Verify menu navigation
- Verify configuration persistence
- Test firmware download on dev board

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| ISO-TP bugs break CAN bus | Extensive unit tests, gradual rollout |
| Shearwater quirk not handled | Test with real Petrel early (Phase 2) |
| Flash write bricks device | Authentication required, test on dev board first |
| Backward compat broken | Parallel protocol support, regression tests |
| Memory region errors | Strict validation, read-only testing first (Phase 5) |
| Timing issues (watchdog) | Profile with timer, refresh watchdog in loops |

## Success Criteria

1. All unit tests pass (100+ tests total)
2. Hardware integration tests pass
3. Existing menu.c behavior preserved (all legacy tests pass)
4. Can read/write all 8 settings via UDS
5. Can upload memory regions (BLOCK1, BLOCK3)
6. Can download firmware (on dev board)
7. Code review approved
8. Documentation complete

## File Summary

### New Files (18)
- `STM32/Core/Src/DiveCAN/uds/isotp.c/h`
- `STM32/Core/Src/DiveCAN/uds/uds.c/h`
- `STM32/Core/Src/DiveCAN/uds/uds_services.c/h`
- `STM32/Core/Src/DiveCAN/uds/uds_settings.c/h`
- `STM32/Core/Src/DiveCAN/uds/uds_memory.c/h`
- `STM32/Tests/menu_tests.cpp`
- `STM32/Tests/isotp_tests.cpp`
- `STM32/Tests/uds_tests.cpp`
- `HW Testing/Tests/test_uds.py`

### Modified Files (5)
- `STM32/Core/Src/DiveCAN/DiveCAN.c` (add UDS routing in RespMenu)
- `STM32/Core/Src/DiveCAN/Transciever.c` (add ISO-TP TX helpers)
- `STM32/Tests/Makefile` (add new test files)
- `CLAUDE.md` (document UDS architecture)
- `HW Testing/Tests/conftest.py` (add UDS fixtures if needed)

### Critical Path Files (5 most important)
1. `STM32/Core/Src/DiveCAN/uds/isotp.c` - Foundation for all UDS
2. `STM32/Core/Src/DiveCAN/DiveCAN.c` - Integration/routing
3. `STM32/Core/Src/DiveCAN/uds/uds_settings.c` - Primary user feature
4. `STM32/Core/Src/configuration.c` - Config validation integration
5. `STM32/Tests/isotp_tests.cpp` - Prevent subtle multi-frame bugs
