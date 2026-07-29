import { defineConfig } from 'vitest/config';

export default defineConfig({
  test: {
    environment: 'jsdom',
    globals: true,
    include: ['src/**/*.test.js', 'tests/**/*.test.js'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'html', 'lcov'],
      include: ['src/**/*.js'],
      exclude: [
        'src/index.js',
        'src/**/*.test.js',
        // Pure-DOM UI adapters — exercised manually in the browser, no unit-test surface
        'src/diagnostics/PlotManager.js',
        'src/diagnostics/CellUIAdapter.js',
        'src/DeviceManager.js'
      ]
    }
  }
});
