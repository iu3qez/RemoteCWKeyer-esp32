# WebUI TODO

## Completato

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
- [x] Task 9: Svelte 5 scaffold con Vite
- [x] Task 10: TypeScript API client
- [x] Task 11: System page
- [x] Task 12: Config page
- [x] Task 13: Decoder page con SSE

### Phase 3: Integration
- [x] Task 14: WebUI init in main.c
- [x] Task 15: Decoder SSE push in bg_task.c

## Da Fare

### WiFi Integration
- [ ] Implementare modulo `keyer_wifi/` con AP mode e STA mode
- [ ] Aggiungere parametri WiFi a `parameters.yaml` (SSID, password, mode)
- [ ] Condizionare `webui_start()` su WiFi ready in `main.c`
- [ ] Completare `api_status_handler()` con stato WiFi reale

### Timeline Page
- [ ] Implementare `Timeline.svelte` con visualizzazione real-time
- [ ] Canvas/SVG per keying waveform display
- [ ] Integrare timeline SSE events (paddle, keying, decoded, gap)

### Keyer Page
- [ ] Implementare `Keyer.svelte` per text keyer
- [ ] UI per invio testo libero
- [ ] Gestione memory slots (M1-M8)
- [ ] Abort/pause/resume controls

### Firmware Update
- [ ] Implementare OTA update via WebUI
- [ ] Upload firmware binary
- [ ] Progress indicator
- [ ] Rollback support

### Polish
- [ ] Dark mode toggle
- [ ] Mobile responsive improvements
- [ ] Connection status indicator in navbar
- [ ] Reconnect logic per SSE streams
- [ ] Error toast notifications

## API Endpoints

### Implementati
| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| GET | `/api/status` | Stato WiFi (stub) |
| GET | `/api/system/stats` | Uptime, heap, tasks |
| POST | `/api/system/reboot` | Riavvia device |
| GET | `/api/config/schema` | Schema parametri JSON |
| GET | `/api/config` | Valori correnti |
| POST | `/api/parameter` | Modifica singolo parametro |
| POST | `/api/config/save` | Salva in NVS |
| GET | `/api/decoder/status` | Stato decoder |
| POST | `/api/decoder/enable` | Abilita/disabilita |
| GET | `/api/decoder/stream` | SSE decoded chars |
| GET | `/api/timeline/config` | WPM corrente |
| GET | `/api/timeline/stream` | SSE keying events |

### Da Implementare
| Method | Endpoint | Descrizione |
|--------|----------|-------------|
| POST | `/api/keyer/send` | Invia testo |
| POST | `/api/keyer/message` | Invia memory slot |
| POST | `/api/keyer/abort` | Interrompi |
| GET | `/api/keyer/status` | Stato text keyer |
| POST | `/api/firmware/upload` | OTA update |

## Note Tecniche

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
# Proxy configurato per /api -> http://192.168.4.1
```

### Asset Size Budget
Target: < 50KB gzipped totale
- app.js: ~15KB gzipped
- index.css: ~1KB gzipped
- index.html: ~300B gzipped
