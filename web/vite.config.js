import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// `npm run dev` serves the app on :5173 and forwards /api to the C++ server on :8080, so the browser sees one origin
// (no CORS). `npm run build` writes the finished app to dist/, which the server serves with --web web/dist.
export default defineConfig({
  plugins: [svelte()],
  server: { proxy: { '/api': 'http://127.0.0.1:8080' } },
  build: { chunkSizeWarningLimit: 700 },
  test: { include: ['tests/**/*.test.js'] },
});
