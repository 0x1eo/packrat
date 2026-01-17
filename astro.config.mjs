// @ts-check

import mdx from '@astrojs/mdx';
import sitemap from '@astrojs/sitemap';
import { defineConfig } from 'astro/config';

// https://astro.build/config
export default defineConfig({
	site: 'https://blog.0x1eo.dev',
	base: '/packrat',
	integrations: [mdx(), sitemap()],
});
