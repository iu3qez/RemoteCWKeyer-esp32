export interface CWNetStatus {
  state: string;
  latency_ms: number;
}

export interface DeviceStatus {
  mode: string;
  ip: string;
  ready: boolean;
  cwnet?: CWNetStatus;
}

export interface SystemUptime {
  hours: number;
  minutes: number;
  seconds: number;
  total_seconds: number;
}

export interface HeapInfo {
  free_bytes: number;
  minimum_free_bytes: number;
  total_bytes: number;
  largest_free_block: number;
}

export interface TaskInfo {
  name: string;
  state: string;
  priority: number;
  stack_hwm: number;
}

export interface SystemStats {
  uptime: SystemUptime;
  heap: HeapInfo;
  tasks: TaskInfo[];
}

export interface DecoderStatus {
  enabled: boolean;
  wpm: number;
  pattern: string;
  text: string;
}

export interface TimelineConfig {
  wpm: number;
  wpm_source: string;
}

export interface ParameterMeta {
  name: string;
  type: string;
  widget: string;
  min?: number;
  max?: number;
  unit?: string;
  description: string;
  values?: Array<{ name: string; description: string }>;
}

export interface ConfigSchema {
  parameters: ParameterMeta[];
}

export type ConfigValues = Record<string, Record<string, number | boolean | string>>;

export type TextKeyerState = 'IDLE' | 'SENDING' | 'PAUSED';

export interface TextKeyerStatus {
  state: TextKeyerState;
  sent: number;
  total: number;
  progress: number;
}

export interface MemorySlot {
  slot: number;
  text: string;
  label: string;
}

export interface VpnStats {
  handshakes: number;
  last_handshake_us: number;
}

export interface VpnStatus {
  state: string;
  connected: boolean;
  stats?: VpnStats;
}

export interface FirmwareStatus {
  version: string;
  date: string;
  time: string;
  idf_ver: string;
  running_partition: string;
  next_partition: string;
  rollback_available: boolean;
  pending_verify: boolean;
  ota_state: 'IDLE' | 'UPLOADING' | 'DOWNLOADING' | 'DONE' | 'ERROR';
  ota_progress: number;
  ota_bytes_written: number;
  ota_total_size: number;
  ota_error?: string;
}
