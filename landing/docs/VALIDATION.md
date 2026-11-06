# Landing page validation

2026-11-06. Local Node 26.12.0, Next.js 16.4.3, React 19.3.1, TypeScript 5.9.3.

| Check | Result |
| --- | --- |
| Dependency installation from local npm cache | Pass: clean `npm ci --offline` using registry tarball integrity from the lockfile |
| `npm run typecheck` | Pass |
| `npm run build` | Pass: production Webpack build and TypeScript compiler API; `/`, `/guide`, and 404 prerendered |
| Generated page metadata and heading structure | Pass: one H1 and a description per content route |
| Internal links and local assets | Pass: 43 generated references inspected across homepage and guide |
| Source archive checksum and member safety | Pass: deterministic archive, no traversal/link members, SHA256SUMS matches |
| Current product code in downloadable archive | Pass: every current `linux/src/*.cpp` and `*.h` matches archive bytes |
| E2E test discovery | Pass: ten desktop/mobile test cases are discovered |
| Browser interaction, screenshots, responsive visual review | **Blocked by environment; not claimed passed** |

## Concrete browser blockers

`next start --hostname 0.0.0.0 --port 3107` fails with `listen EPERM: operation not permitted`. Direct Playwright Chromium launch also fails when Crashpad tries to initialize a socket: `setsockopt: Operation not permitted`. No connected CUA browser was available. These are execution restrictions; a successful production build is not evidence of responsive appearance or interaction success.

Run the real tests in a normal terminal:

```bash
cd /home/abullah/Projects/OpenEye/landing
npm run build
PLAYWRIGHT_CHROMIUM_EXECUTABLE=/usr/bin/chromium npm run test:e2e
```

If Chromium is not installed, use `npx playwright install chromium` and omit `PLAYWRIGHT_CHROMIUM_EXECUTABLE`. The test configuration starts an isolated production server on port 3107. Screenshots are saved under `test-results/`. It checks layout overflow, browser errors, presets, start/end, custom duration limits, OS selection, copy feedback, FAQ, guide navigation, downloads and keyboard skip navigation.

## Implementation details affecting verification

- TypeScript 5.9's compiler API is explicitly selected in Next's config; this preserves build-time type checks while avoiding detached CLI stdout capture in this sandbox.
- The lockfile selects exact registry versions and integrity hashes. Network resolution was unavailable, so dependencies were materialized from already cached official npm tarballs. No dependency security audit is claimed.
- All runtime visuals are inline SVG/CSS. There are no external media dependencies or trackers. The product preview is a browser-only simulation, visibly labeled, and does not talk to live power services.
- Publication, canonical production domain, hosted release distribution, real install on customer machines and broad Linux runtime verification are outside the landing-page checks.

## Reference-directed redesign

The current page supersedes the initial split hero. It now includes the original wake-eye illustration, day/night theme, five operational chapters and a reading-position navigator. Production build, typecheck, static checks and ten-test discovery passed after the redesign. Browser interaction and visual verification remain blocked by the same recorded environment prerequisites.
