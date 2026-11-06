import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "EverEye — Your machine. A little more time.",
  description: "Keep your Linux machine awake for the work that needs more time. Timed sessions, tray controls, custom durations, and battery safeguards. Free, open source, and native Qt.",
  openGraph: { title: "EverEye — Your machine. A little more time.", description: "A small, native Linux utility for timed awake sessions.", type: "website", images: [{ url: "/social.svg", width: 1200, height: 630 }] },
  robots: { index: true, follow: true },
  icons: { icon: "/icon.svg" },
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return <html lang="en"><body>{children}</body></html>;
}
