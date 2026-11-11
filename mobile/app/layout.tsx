import type { Metadata, Viewport } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "EverEye AI sessions",
  description: "Watch and control EverEye AI sessions from your phone.",
  manifest: "/manifest.webmanifest",
  icons: { icon: "/icon.svg", apple: "/icon.svg" },
  appleWebApp: { capable: true, title: "EverEye", statusBarStyle: "black-translucent" },
};

export const viewport: Viewport = {
  themeColor: "#12181a",
  width: "device-width",
  initialScale: 1,
  viewportFit: "cover",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>
        {children}
        <script
          dangerouslySetInnerHTML={{
            __html: `if ("serviceWorker" in navigator) navigator.serviceWorker.register("/sw.js").catch(() => {});`,
          }}
        />
      </body>
    </html>
  );
}
