import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The pedalboard binary serves static files from ../web (resolved next to the
// executable at runtime) and deploy.sh rsyncs that same dir to the Pi. So we
// build STRAIGHT into ../web -- no copy step, no separate deploy path.
//
// `npm run dev` runs a hot-reloading dev server on the Mac and proxies /api +
// the SSE stream to a pedalboard running on localhost:8080, so the live UI works
// against the real device while you iterate.
export default defineConfig({
  plugins: [svelte()],
  build: {
    outDir: '../web',
    emptyOutDir: true,
  },
  server: {
    proxy: {
      '/api': { target: 'http://localhost:8080', changeOrigin: true },
    },
  },
});
