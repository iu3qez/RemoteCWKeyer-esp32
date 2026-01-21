/**
 * Theme Store
 *
 * Manages the current theme, syncs with device NVS via API.
 */

import { themes, defaultTheme, applyTheme, type Theme } from '../themes';
import { api } from '../api';

// Current theme state
let currentTheme: Theme = defaultTheme;
let initialized = false;

/**
 * Get the current theme
 */
export function getTheme(): Theme {
  return currentTheme;
}

/**
 * Initialize theme from device config
 * Call this on app mount
 */
export async function initTheme(): Promise<void> {
  if (initialized) return;

  try {
    const config = await api.getConfig();
    const themeId = config?.system?.ui_theme as string;

    if (themeId && themes[themeId]) {
      currentTheme = themes[themeId];
    } else {
      currentTheme = defaultTheme;
    }
  } catch (error) {
    console.warn('Failed to load theme from device, using default:', error);
    currentTheme = defaultTheme;
  }

  applyTheme(currentTheme);
  initialized = true;
}

/**
 * Set and apply a new theme
 * @param themeId - The theme ID (e.g., 'matrix_green', 'amber_terminal')
 * @param save - Whether to save to device NVS (default: true)
 */
export async function setTheme(themeId: string, save = true): Promise<void> {
  const theme = themes[themeId];
  if (!theme) {
    console.error(`Unknown theme: ${themeId}`);
    return;
  }

  currentTheme = theme;
  applyTheme(theme);

  if (save) {
    try {
      await api.setParameter('system.ui_theme', themeId);
      await api.saveConfig();
    } catch (error) {
      console.error('Failed to save theme to device:', error);
    }
  }
}

/**
 * Get all available themes
 */
export function getAvailableThemes(): Theme[] {
  return Object.values(themes);
}
