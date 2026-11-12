import type { NextConfig } from "next";
const config: NextConfig = {
  // Static export so the EverEye gateway can serve the PWA from disk and the
  // same files can be hosted anywhere else.
  output: "export",
  images: { unoptimized: true },
  poweredByHeader: false,
  experimental: { useTypeScriptCli: false },
};
export default config;
