# EverEye landing page

Next.js App Router, React, TypeScript, and plain responsive CSS. All visuals are local SVG/CSS. No third-party fonts, analytics, authentication, or backend are required.

Current design: a dark, editorial walkthrough inspired by the user-supplied getinsomnia.app reference, with original eye artwork, copy, chapter layouts and a day/night palette. The hero eye responds to pointer, scroll and keyboard activation; the bottom chapter navigation tracks the reading position.

```bash
cd landing
npm ci
npm run dev
```

Open http://localhost:3000. Production: `npm run build && npm start`. Use Node.js 20.9 or newer.

## Verification

```bash
npm run typecheck
npm run build
npm run test:e2e
```

E2E uses Playwright Chromium. Install it with `npx playwright install chromium`, or set `PLAYWRIGHT_CHROMIUM_EXECUTABLE` to an installed Chromium executable. The config starts an isolated local production server on port 3107.

## Content and assets

- `docs/RESEARCH.md`: primary-source design/content research, evidence ledger, implementation plan.
- `components/product-demo.tsx`: isolated browser simulation; never controls live power services.
- `components/install.tsx`: dependency selector and copy controls.
- `/guide`: local quick-start guide.
- `public/downloads/`: frozen current source export plus SHA256SUMS. Regenerate using `python3 scripts/export-source.py` when Linux sources change.
- `public/license.txt`: product MIT license.

Only source downloads are offered. Do not add unsupported package claims or fabricated GitHub stars, testimonials, release links, compatibility badges, or usage statistics. Universal packaging is still under validation. Desktop/power limitations are stated beside installation and in FAQs.

No deployment has been performed. Before publishing, verify download provenance and replace the SVG social preview with a raster rendition if required by your social platform. Update version, filename, and commands together on a release.
