import type { NextConfig } from "next";
const config: NextConfig = {
  poweredByHeader: false,
  // Use the compiler API supplied by TypeScript 5.9. This also avoids detached
  // CLI subprocess output capture in restricted build environments.
  experimental: { useTypeScriptCli: false },
};
export default config;
