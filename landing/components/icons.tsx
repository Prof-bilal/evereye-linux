export function Eye({ className = "" }: { className?: string }) {
  return <svg className={className} width="32" height="24" viewBox="0 0 32 24" fill="none" aria-hidden="true"><path d="M2 12S7 4 16 4s14 8 14 8-5 8-14 8S2 12 2 12Z" stroke="currentColor" strokeWidth="1.7"/><circle cx="16" cy="12" r="4" stroke="currentColor" strokeWidth="1.7"/></svg>;
}
export function Arrow({ diagonal = false }: { diagonal?: boolean }) {
  return <svg width="17" height="17" viewBox="0 0 24 24" fill="none" aria-hidden="true" style={diagonal ? { transform: "rotate(-45deg)" } : undefined}><path d="M5 12h14m-6-6 6 6-6 6" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round"/></svg>;
}
export function DownloadIcon() {
  return <svg width="18" height="18" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M12 3v12m-4-4 4 4 4-4M4 16v5h16v-5" stroke="currentColor" strokeWidth="1.7" strokeLinecap="round" strokeLinejoin="round"/></svg>;
}
