export interface DeviceStatus {
  mode: string;
  ip: string;
  ready: boolean;
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
