import preact from '@preact/preset-vite';
import { defineConfig } from 'vite';
import { viteSingleFile } from "vite-plugin-singlefile";
import htmlMinifier from 'vite-plugin-html-minifier';
import { compression } from 'vite-plugin-compression2';

// https://vite.dev/config/
export default defineConfig(({ command }) => {
  const isProduction = command === 'build';
  return {
    plugins: isProduction ? [
      preact(),
      viteSingleFile(),
      htmlMinifier({
        minify: true,
      }),
      compression({
        algorithms: ['gzip'], // emit both .gz and .br
        threshold: 1024, // only compress >1KB
        deleteOriginalAssets: false, // keep originals for fallback
      })] : [
      preact(),
    ],
  };
});
