# WebUI TODO

## Done

### Phase 1: Backend
- [x] Task 1: Component structure (`components/keyer_webui/`)
- [x] Task 2: Asset embedding script (`scripts/embed_assets.py`)
- [x] Task 3: Static asset serving with SPA routing
- [x] Task 4: Config schema JSON generation
- [x] Task 5: Config API endpoints
- [x] Task 6: System API endpoints
- [x] Task 7: Decoder API endpoints
- [x] Task 8: SSE infrastructure

### Phase 2: Frontend
- [x] Task 9: Svelte 5 scaffold with Vite
- [x] Task 10: TypeScript API client
- [x] Task 11: System page
- [x] Task 12: Config page
- [x] Task 13: Decoder page with SSE

### Phase 3: Integration
- [x] Task 14: WebUI init in main.c
- [x] Task 15: Decoder SSE push in bg_task.c

## To Do

### WiFi Integration
- [ ] Implement the `keyer_wifi/` module with AP mode and STA mode
- [ ] Add WiFi parameters to `parameters.yaml` (SSID, password, mode)
- [ ] Make `webui_start()` conditional on WiFi ready in `main.c`
- [ ] Complete `api_status_handler()` with the real WiFi status

### Timeline Page
- [ ] Implement `Timeline.svelte` with real-time visualization
- [ ] Canvas/SVG for keying waveform display
- [ ] Integrate timeline SSE events (paddle, keying, decoded, gap)

### Keyer Page
- [ ] Implement `Keyer.svelte` for the text keyer
- [ ] UI for sending free text
- [ ] Memory slot management (M1-M8)
- [ ] Abort/pause/resume controls

### Firmware Update
- [ ] Implement OTA update via WebUI
- [ ] Upload firmware binary
- [ ] Progress indicator
- [ ] Rollback support

### Polish
- [ ] Dark mode toggle
- [ ] Mobile responsive improvements
- [ ] Connection status indicator in navbar
- [ ] Reconnect logic for SSE streams
- [ ] Error toast notifications

## API Endpoints

### Implemented
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | WiFi status (stub) |
| GET | `/api/system/stats` | Uptime, heap, tasks |
| POST | `/api/system/reboot` | Reboot the device |
| GET | `/api/config/schema` | JSON parameter schema |
| GET | `/api/config` | Current values |
| POST | `/api/parameter` | Change a single parameter |
| POST | `/api/config/save` | Save to NVS |
| GET | `/api/decoder/status` | Decoder status |
| POST | `/api/decoder/enable` | Enable/disable |
| GET | `/api/decoder/stream` | SSE decoded chars |
| GET | `/api/timeline/config` | Current WPM |
| GET | `/api/timeline/stream` | SSE keying events |

### To Implement
| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/keyer/send` | Send text |
| POST | `/api/keyer/message` | Send memory slot |
| POST | `/api/keyer/abort` | Abort |
| GET | `/api/keyer/status` | Text keyer status |
| POST | `/api/firmware/upload` | OTA update |

## Technical Notes

### Build Frontend
```bash
cd components/keyer_webui/frontend
npm install
npm run build
cd ../../..
python3 components/keyer_webui/scripts/embed_assets.py \
  components/keyer_webui/frontend/dist \
  components/keyer_webui/src/assets.c
idf.py build
```

### Development Mode
```bash
cd components/keyer_webui/frontend
npm run dev
# Proxy configured for /api -> http://192.168.4.1
```

### Asset Size Budget
Target: < 50KB gzipped total
- app.js: ~15KB gzipped
- index.css: ~1KB gzipped
- index.html: ~300B gzipped
