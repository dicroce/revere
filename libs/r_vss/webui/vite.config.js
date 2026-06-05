import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],

  // During development (npm run dev), proxy API calls to the running Revere instance.
  // In production the Vue app is served by Revere itself, so no proxy is needed.
  server: {
    proxy: {
      '/cameras':          'http://localhost:8088',
      '/key_frame':        'http://localhost:8088',
      '/contents':         'http://localhost:8088',
      '/motion_events':    'http://localhost:8088',
      '/analytics':        'http://localhost:8088',
      '/jpg':              'http://localhost:8088',
      '/video':            'http://localhost:8088',
      '/webp':             'http://localhost:8088',
      // Record flow + auth
      '/auth_status':      'http://localhost:8088',
      '/login':            'http://localhost:8088',
      '/set_password':     'http://localhost:8088',
      '/camera_profiles':  'http://localhost:8088',
      '/measure_camera':   'http://localhost:8088',
      '/measure_progress': 'http://localhost:8088',
      '/measure_result':   'http://localhost:8088',
      '/configure_camera': 'http://localhost:8088',
    }
  },

  build: {
    outDir: 'dist',
    emptyOutDir: true
  }
})
