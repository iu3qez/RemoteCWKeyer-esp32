/**
 * CW Keyer WebUI Theme System
 *
 * Each theme maintains the CRT terminal aesthetic with different phosphor colors.
 * All colors are designed for proper contrast ratios (WCAG AA compliance).
 */

export interface Theme {
  id: string;
  name: string;
  description: string;
  colors: {
    // Backgrounds
    bgPrimary: string;
    bgSecondary: string;
    bgTertiary: string;
    bgHover: string;
    bgCard: string;

    // Text hierarchy
    textBright: string;
    textPrimary: string;
    textSecondary: string;
    textDim: string;

    // Borders
    borderColor: string;
    borderDim: string;

    // Accent colors (semantic)
    accentPrimary: string;  // Main brand color
    accentAmber: string;    // Warnings, highlights
    accentCyan: string;     // Info, links
    accentRed: string;      // Errors, destructive
    accentMagenta: string;  // Special

    // Glows (for CRT effect)
    glowPrimary: string;
    glowAmber: string;
    glowCyan: string;
  };
}

// ═══════════════════════════════════════════════════════════════════════════
// MATRIX GREEN - Classic hacker terminal (current default)
// ═══════════════════════════════════════════════════════════════════════════
export const matrixGreen: Theme = {
  id: 'matrix_green',
  name: 'Matrix Green',
  description: 'Classic green phosphor terminal',
  colors: {
    // Backgrounds - deep blacks with slight blue tint
    bgPrimary: '#0a0a0a',
    bgSecondary: '#0d1117',
    bgTertiary: '#161b22',
    bgHover: '#1c2128',
    bgCard: '#0d1117',

    // Text - green phosphor hierarchy
    textBright: '#7fff7f',
    textPrimary: '#00ff41',
    textSecondary: '#4ec968',
    textDim: '#6b8f71',

    // Borders
    borderColor: '#00ff41',
    borderDim: '#2f5f3f',

    // Accents
    accentPrimary: '#00ff41',
    accentAmber: '#ffb000',
    accentCyan: '#00d4ff',
    accentRed: '#ff4757',
    accentMagenta: '#ff6bcb',

    // Glows
    glowPrimary: '0 0 10px #00ff41, 0 0 20px #00ff4133',
    glowAmber: '0 0 10px #ffb000, 0 0 20px #ffb00033',
    glowCyan: '0 0 10px #00d4ff, 0 0 20px #00d4ff33',
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// AMBER TERMINAL - Warm retro CRT (IBM 3278 style)
// ═══════════════════════════════════════════════════════════════════════════
export const amberTerminal: Theme = {
  id: 'amber_terminal',
  name: 'Amber Terminal',
  description: 'Warm amber CRT like classic IBM terminals',
  colors: {
    // Backgrounds - warm dark browns
    bgPrimary: '#0a0908',
    bgSecondary: '#12100d',
    bgTertiary: '#1a1714',
    bgHover: '#221e1a',
    bgCard: '#12100d',

    // Text - amber phosphor hierarchy
    textBright: '#ffdd88',
    textPrimary: '#ffb000',
    textSecondary: '#d4943c',
    textDim: '#a08060',

    // Borders
    borderColor: '#ffb000',
    borderDim: '#5a4020',

    // Accents
    accentPrimary: '#ffb000',
    accentAmber: '#ff8c00',    // Darker amber for contrast
    accentCyan: '#88ccff',     // Softer cyan
    accentRed: '#ff6b6b',
    accentMagenta: '#ffaa88',

    // Glows
    glowPrimary: '0 0 10px #ffb000, 0 0 20px #ffb00033',
    glowAmber: '0 0 10px #ff8c00, 0 0 20px #ff8c0033',
    glowCyan: '0 0 10px #88ccff, 0 0 20px #88ccff33',
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// CYAN ICE - Cool blue terminal (scientific/medical equipment style)
// ═══════════════════════════════════════════════════════════════════════════
export const cyanIce: Theme = {
  id: 'cyan_ice',
  name: 'Cyan Ice',
  description: 'Cool blue terminal for that scientific look',
  colors: {
    // Backgrounds - cool dark blues
    bgPrimary: '#080a0c',
    bgSecondary: '#0c1218',
    bgTertiary: '#141c24',
    bgHover: '#1a242e',
    bgCard: '#0c1218',

    // Text - cyan phosphor hierarchy
    textBright: '#aaffff',
    textPrimary: '#00d4ff',
    textSecondary: '#5cb8d4',
    textDim: '#6a9aaa',

    // Borders
    borderColor: '#00d4ff',
    borderDim: '#204858',

    // Accents
    accentPrimary: '#00d4ff',
    accentAmber: '#ffcc00',     // Warm contrast
    accentCyan: '#00ffff',      // Brighter cyan
    accentRed: '#ff5577',
    accentMagenta: '#cc88ff',

    // Glows
    glowPrimary: '0 0 10px #00d4ff, 0 0 20px #00d4ff33',
    glowAmber: '0 0 10px #ffcc00, 0 0 20px #ffcc0033',
    glowCyan: '0 0 10px #00ffff, 0 0 20px #00ffff33',
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// PHOSPHOR WHITE - Classic monochrome terminal
// ═══════════════════════════════════════════════════════════════════════════
export const phosphorWhite: Theme = {
  id: 'phosphor_white',
  name: 'Phosphor White',
  description: 'Classic white/gray monochrome terminal',
  colors: {
    // Backgrounds - pure dark grays
    bgPrimary: '#0a0a0a',
    bgSecondary: '#121212',
    bgTertiary: '#1a1a1a',
    bgHover: '#222222',
    bgCard: '#121212',

    // Text - white phosphor hierarchy
    textBright: '#ffffff',
    textPrimary: '#e0e0e0',
    textSecondary: '#b0b0b0',
    textDim: '#808080',

    // Borders
    borderColor: '#e0e0e0',
    borderDim: '#404040',

    // Accents
    accentPrimary: '#e0e0e0',
    accentAmber: '#ffcc66',
    accentCyan: '#66ccff',
    accentRed: '#ff6666',
    accentMagenta: '#cc99ff',

    // Glows
    glowPrimary: '0 0 10px #e0e0e0, 0 0 20px #e0e0e033',
    glowAmber: '0 0 10px #ffcc66, 0 0 20px #ffcc6633',
    glowCyan: '0 0 10px #66ccff, 0 0 20px #66ccff33',
  }
};

// ═══════════════════════════════════════════════════════════════════════════
// Theme registry
// ═══════════════════════════════════════════════════════════════════════════
export const themes: Record<string, Theme> = {
  'matrix_green': matrixGreen,
  'amber_terminal': amberTerminal,
  'cyan_ice': cyanIce,
  'phosphor_white': phosphorWhite,
};

export const themeList: Theme[] = Object.values(themes);

export const defaultTheme = matrixGreen;

/**
 * Apply a theme by setting CSS custom properties on the document root
 */
export function applyTheme(theme: Theme): void {
  const root = document.documentElement;
  const { colors } = theme;

  // Backgrounds
  root.style.setProperty('--bg-primary', colors.bgPrimary);
  root.style.setProperty('--bg-secondary', colors.bgSecondary);
  root.style.setProperty('--bg-tertiary', colors.bgTertiary);
  root.style.setProperty('--bg-hover', colors.bgHover);
  root.style.setProperty('--bg-card', colors.bgCard);

  // Text
  root.style.setProperty('--text-bright', colors.textBright);
  root.style.setProperty('--text-primary', colors.textPrimary);
  root.style.setProperty('--text-secondary', colors.textSecondary);
  root.style.setProperty('--text-dim', colors.textDim);

  // Borders
  root.style.setProperty('--border-color', colors.borderColor);
  root.style.setProperty('--border-dim', colors.borderDim);

  // Accents
  root.style.setProperty('--accent-green', colors.accentPrimary);
  root.style.setProperty('--accent-primary', colors.accentPrimary);
  root.style.setProperty('--accent-amber', colors.accentAmber);
  root.style.setProperty('--accent-cyan', colors.accentCyan);
  root.style.setProperty('--accent-red', colors.accentRed);
  root.style.setProperty('--accent-magenta', colors.accentMagenta);

  // Glows
  root.style.setProperty('--glow-green', colors.glowPrimary);
  root.style.setProperty('--glow-primary', colors.glowPrimary);
  root.style.setProperty('--glow-amber', colors.glowAmber);
  root.style.setProperty('--glow-cyan', colors.glowCyan);

  // Also update body background for the main page color
  document.body.style.background = colors.bgPrimary;
  document.body.style.color = colors.textPrimary;
}
