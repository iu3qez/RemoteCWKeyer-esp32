import type {
  DeviceStatus,
  SystemStats,
  DecoderStatus,
  TimelineConfig,
  ConfigSchema,
  ConfigValues
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

  // SSE helpers
  connectDecoderStream(onChar: (char: string, wpm: number) => void, onWord: () => void): EventSource {
    const es = new EventSource(`${this.baseUrl}/api/decoder/stream`);

    es.addEventListener('char', (e) => {
      const data = JSON.parse(e.data);
      onChar(data.char, data.wpm);
    });

    es.addEventListener('word', () => {
      onWord();
    });

    return es;
  }

  connectTimelineStream(onEvent: (type: string, data: unknown) => void): EventSource {
    const es = new EventSource(`${this.baseUrl}/api/timeline/stream`);

    ['paddle', 'keying', 'decoded', 'gap'].forEach(eventType => {
      es.addEventListener(eventType, (e) => {
        onEvent(eventType, JSON.parse(e.data));
      });
    });

    return es;
  }
}

export const api = new ApiClient();
