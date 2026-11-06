# EverEye landing: design and content research

## User-directed reference update

The user subsequently supplied [getinsomnia.app](https://getinsomnia.app/) and asked for a related feel without copying. Reviewed its live page on 2026-11-06. Its content presents a small interactive tray control, a numbered sequence, and operational panels for power, recovery, and installation. Adopted the general principle of demonstrating the utility through a narrative. The replacement EverEye page uses an original pointer-reactive eye illustration, dark olive and warm amber palette, day/night control, its own five-chapter order and copy, and a compact chapter navigator. The earlier cream split hero and landscape are superseded.

No reference HTML, styles, artwork, or animation code was copied. Only the Linux feature evidence below supports product claims. In particular, the macOS reference's app freezing, audio restoration, hotspot, tmux and thermal features are not presented as EverEye features. The independent supervisor and descriptor release model are explained using local code evidence.

New interaction checks cover keyboard activation of the eye, theme switching and chapter navigation. Reduced-motion preference disables transition animation and pointer tracking. Browser execution remains subject to the separately recorded socket blocker; source inspection and static checks are not described as a visual comparison pass.

Researched 2026-11-06. Outcome: an original, credible product landing page in `landing/`, using Next.js. All product claims are checked against the local Linux implementation. No deployment or release publication is part of this task.

## Audience and decisions

Primary audience: Linux users running builds, downloads, scripts, and renders who need a bounded awake session. Their first questions are what stays awake, how long, what happens to battery and screen locking, and how to install it. The site answers these before asking for a download. The visual direction is editorial and quiet: warm off-white, forest ink, a limited olive accent, large human-readable typography, fine rules, and an original landscape framing a functional tray preview. Avoid glass effects, giant glows, generic feature-card grids, fake testimonials, invented stars, and unsupported universal compatibility.

## Primary-source reference ledger

| Source | Observed content/pattern | Applied design judgment |
| --- | --- | --- |
| [LocalSend](https://localsend.org/) | Direct benefit statement; downloads lead into features and a short workflow | Lead with the interrupted-work problem, show the product, then explain three concrete benefits and steps. Do not borrow its adoption statistics. |
| [Raycast](https://www.raycast.com/) | Outcome-led headline, visible product interactions, features shown in use | Make the tray experience the hero, with interactive start/end/presets rather than an unrelated abstract illustration. |
| [Obsidian](https://obsidian.md/) | Short human-centered introduction, product preview, explicit local-device/privacy explanation | Use concise copy and local execution evidence. Do not invent an account system or a privacy certification. |
| [Zed](https://zed.dev/) | Native developer-tool positioning, product demonstration, download/source access | Include a secondary CLI example and an inspectable source archive. Website observations are references, not claims about EverEye performance. |
| [Next.js App Router installation](https://nextjs.org/docs/app/getting-started/installation) | Root layout, file-based pages, React dependencies; Node 20.9+ | Use App Router with server-rendered content and client components only for demo/copy controls. |
| [W3C contrast criterion](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) | AA text contrast threshold: 4.5:1 normally, 3:1 large text | Use dark text on light surfaces and visible focus states; verify keyboard access and small viewport layout. |

References were reviewed for content hierarchy and interaction patterns. The design is an original synthesis, not a copy or a claim that these references prove a conversion lift. Research tools supplied page text; final visual validation must inspect the actual local site.

## Claim-to-evidence map

| Website statement | Local evidence |
| --- | --- |
| Native Qt, Linux, timed sessions | `linux/CMakeLists.txt`, `src/main.cpp`, `src/core.cpp` |
| One minute to 30 days, extension and custom durations | `src/core.h`, `src/window.cpp`, `tests/test_insomnia.cpp` |
| Tray panel and CLI | `src/window.cpp`, `src/main.cpp` |
| Default cutoff below 10%; adjustable | `src/core.h`, `src/core.cpp`, `src/window.cpp` |
| Optional lid policy; desktop differences | `src/platform.cpp`, `docs/PORTABILITY-RESEARCH.md` |
| Interrupted sessions never automatically resume | `src/core.cpp`, `src/guardian.cpp`, regression checks |
| No screen-lock idle inhibitor | `src/platform.cpp` sleep/lid inhibitor targets |
| MIT and no subscription/account requirement | `LICENSE`, native implementation has no account/auth flow |
| Broad distro packages remain under validation | `docs/PORTABILITY-RESULTS.md`, matrix/audit reports |

We deliberately offer a fresh source export, not a stale host-linked archive. The existing remote repository identifies the macOS origin and is not proof the current Linux work is published there. No fictional release URL is used. The marketing terminal shows illustrative prose, explicitly labeled; actual CLI replies are JSON.

## Execution plan and acceptance

1. Inspect product/source and references; record evidence here.
2. Build `app/`, reusable icons, interactive tray simulation and installation selector. Local assets only, no external runtime fonts.
3. Export current Linux source with deterministic archiving, checksums, license, and a local quick-start guide. Verify archive members and real download links.
4. Run TypeScript and production build; use Playwright for desktop/mobile, presets, custom validation, session state, FAQ, dependency selection and copy UI. Inspect screenshots for overflow and hierarchy.
5. Deliver folder, startup command, research artifact and remaining environmental limitations. Done means the site builds, renders, works across viewports, and has honest working actions. Publishing is separate.

## State and interaction matrix

- Initial: server-rendered complete content; demo starts with clearly labeled illustrative session.
- Active/ended: timer runs, end stops it, preset restarts or extends.
- Invalid custom input: browser bounds and visible alert; bounded total.
- Install: OS selection updates dependency command; copy succeeds or gives manual-copy fallback.
- FAQ: native `details/summary`, keyboard operable without client JS.
- Keyboard: skip link, focus rings, accessible button labels and real anchors.
- Mobile: single column, preserved product preview and install controls; no horizontally clipped content.
- Reduced motion: smooth scrolling and transition effects are disabled.
- No live power integration exists on the website; preview state stays in memory.

## Open release considerations

Installation commands target distro package names and require verification on the chosen host; they do not prove the full portability goal is done. The product has no finalized public Linux release remote or domain. Production domain/canonical, package channels and social-card raster export can be set when publication is requested. Do not replace these unknowns with invented availability.
