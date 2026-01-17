import type {
  DeviceStatus,
  SystemStats,
  DecoderStatus,
  TimelineConfig,
  ConfigSchema,
  ConfigValues,
  TextKeyerStatus,
  MemorySlot
} from './types';

class ApiClient {
  private baseUrl: string;

  constructor(baseUrl = '') {
    this.baseUrl = baseUrl;
  }

  private async fetchJson<T>(url: string, options?: RequestInit): Promise<T> {
    const response = await fetch(`${this.baseUrl}${url}`, options);
    if (!response.ok) {
      throw new Error(`API error: ${response.statusText}`);
    }
    return response.json();
  }

  // Status
  async getStatus(): Promise<DeviceStatus> {
    return this.fetchJson('/api/status');
  }

  async getSystemStats(): Promise<SystemStats> {
    return this.fetchJson('/api/system/stats');
  }

  async reboot(): Promise<void> {
    await this.fetchJson('/api/system/reboot', { method: 'POST' });
  }

  // Config
  async getSchema(): Promise<ConfigSchema> {
    return this.fetchJson('/api/config/schema');
  }

  async getConfig(): Promise<ConfigValues> {
    return this.fetchJson('/api/config');
  }

  async setParameter(param: string, value: number | boolean | string): Promise<void> {
    await this.fetchJson('/api/parameter', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ param, value })
    });
  }

  async saveConfig(reboot = false): Promise<void> {
    const url = reboot ? '/api/config/save?reboot=true' : '/api/config/save';
    await this.fetchJson(url, { method: 'POST' });
  }

  // Decoder
  async getDecoderStatus(): Promise<DecoderStatus> {
    return this.fetchJson('/api/decoder/status');
  }

  async setDecoderEnabled(enabled: boolean): Promise<void> {
    await this.fetchJson('/api/decoder/enable', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled })
    });
  }

  // Timeline
  async getTimelineConfig(): Promise<TimelineConfig> {
    return this.fetchJson('/api/timeline/config');
  }

  // Text Keyer
  async sendText(text: string): Promise<void> {
    await this.fetchJson('/api/text/send', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ text })
    });
  }

  async getTextStatus(): Promise<TextKeyerStatus> {
    return this.fetchJson('/api/text/status');
  }

  async abortText(): Promise<void> {
    await this.fetchJson('/api/text/abort', { method: 'POST' });
  }

  async pauseText(): Promise<void> {
    await this.fetchJson('/api/text/pause', { method: 'POST' });
  }

  async resumeText(): Promise<void> {
    await this.fetchJson('/api/text/resume', { method: 'POST' });
  }

  async getMemorySlots(): Promise<{ slots: MemorySlot[] }> {
    return this.fetchJson('/api/text/memory');
  }

  async setMemorySlot(slot: number, text: string, label: string): Promise<void> {
    await this.fetchJson('/api/text/memory', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ slot, text, label })
    });
  }

  async playMemorySlot(slot: number): Promise<void> {
    await this.fetchJson('/api/text/play', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ slot })
    });
  }

  // WebSocket streaming
  private ws: WebSocket | null = null;
  private wsCallbacks: WSCallbacks = {};
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private intentionalDisconnect = false;

  // Time synchronization: map device timestamps to browser time
  private deviceBaseTime: number | null = null;
  private browserBaseTime: number | null = null;

  connect(callbacks: WSCallbacks): void {
    // Close existing connection before creating new one
    this.disconnect();
    this.intentionalDisconnect = false;
    this.wsCallbacks = callbacks;
    this.doConnect();
  }

  private doConnect(): void {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = this.baseUrl || location.host;
    const url = `${protocol}//${host}/ws`;

    this.ws = new WebSocket(url);

    this.ws.onopen = () => {
      this.wsCallbacks.onConnect?.();
      if (this.reconnectTimer) {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = null;
      }
    };

    this.ws.onclose = () => {
      this.wsCallbacks.onDisconnect?.();
      // Auto-reconnect after 3 seconds (unless intentionally disconnected)
      if (!this.reconnectTimer && !this.intentionalDisconnect) {
        this.reconnectTimer = setTimeout(() => {
          this.reconnectTimer = null;
          this.doConnect();
        }, 3000);
      }
    };

    this.ws.onerror = () => {
      // Error will trigger onclose
    };

    this.ws.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        this.handleWSMessage(msg);
      } catch {
        console.warn('Invalid WS message:', e.data);
      }
    };
  }

  private handleWSMessage(msg: WSMessage): void {
    // Convert device timestamp to browser time, preserving relative offsets
    let ts: number;
    if (msg.ts !== undefined) {
      const browserNow = Date.now();
      if (this.deviceBaseTime === null) {
        // First event with timestamp: establish baseline
        this.deviceBaseTime = msg.ts;
        this.browserBaseTime = browserNow;
        ts = browserNow;
      } else {
        // Calculate browser time preserving device offset
        ts = this.browserBaseTime! + (msg.ts - this.deviceBaseTime);
        // Don't let events appear in the future
        if (ts > browserNow) {
          ts = browserNow;
        }
      }
    } else {
      ts = Date.now();
    }

    switch (msg.type) {
      case 'decoded':
        this.wsCallbacks.onDecodedChar?.(msg.char, msg.wpm);
        break;
      case 'word':
        this.wsCallbacks.onWord?.();
        break;
      case 'pattern':
        this.wsCallbacks.onPattern?.(msg.pattern);
        break;
      case 'paddle':
        this.wsCallbacks.onPaddle?.(ts, msg.paddle, msg.state);
        break;
      case 'keying':
        this.wsCallbacks.onKeying?.(ts, msg.element, msg.state);
        break;
      case 'gap':
        this.wsCallbacks.onGap?.(ts, msg.gap_type);
        break;
    }
  }

  disconnect(): void {
    this.intentionalDisconnect = true;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    // Reset time sync on disconnect
    this.deviceBaseTime = null;
    this.browserBaseTime = null;
  }

  isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }
}

// WebSocket message types
interface WSMessageDecoded {
  type: 'decoded';
  char: string;
  wpm: number;
}

interface WSMessageWord {
  type: 'word';
}

interface WSMessagePattern {
  type: 'pattern';
  pattern: string;
}

interface WSMessagePaddle {
  type: 'paddle';
  ts: number;
  paddle: number;
  state: number;
}

interface WSMessageKeying {
  type: 'keying';
  ts: number;
  element: number;
  state: number;
}

interface WSMessageGap {
  type: 'gap';
  ts: number;
  gap_type: number;
}

type WSMessage = WSMessageDecoded | WSMessageWord | WSMessagePattern | WSMessagePaddle | WSMessageKeying | WSMessageGap;

export interface WSCallbacks {
  onDecodedChar?: (char: string, wpm: number) => void;
  onWord?: () => void;
  onPattern?: (pattern: string) => void;
  onPaddle?: (ts: number, paddle: number, state: number) => void;
  onKeying?: (ts: number, element: number, state: number) => void;
  onGap?: (ts: number, gapType: number) => void;
  onConnect?: () => void;
  onDisconnect?: () => void;
}

export const api = new ApiClient();
