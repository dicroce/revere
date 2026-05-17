import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],

  // During development (npm run dev), proxy API calls to the running Revere instance.
  // In production the Vue app is served by Revere itself, so no proxy is needed.
  server: {
    proxy: {
      '/cameras':      'http://localhost:10080',
      '/key_frame':    'http://localhost:10080',
      '/contents':     'http://localhost:10080',
      '/motion_events':'http://localhost:10080',
      '/analytics':    'http://localhost:10080',
    }
  },

  build: {
    outDir: 'dist',
    emptyOutDir: true
  }
})
