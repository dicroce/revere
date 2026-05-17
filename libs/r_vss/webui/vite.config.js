import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],

  // During development (npm run dev), proxy API calls to the running Revere instance.
  // In production the Vue app is served by Revere itself, so no proxy is needed.
  server: {
    proxy: {
      '/cameras':      'http://localhost:8088',
      '/key_frame':    'http://localhost:8088',
      '/contents':     'http://localhost:8088',
      '/motion_events':'http://localhost:8088',
      '/analytics':    'http://localhost:8088',
    }
  },

  build: {
    outDir: 'dist',
    emptyOutDir: true
  }
})
