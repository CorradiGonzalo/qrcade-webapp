const TONES = {
  ok: "bg-ok-bg text-ok",
  warn: "bg-warn-bg text-warn",
  danger: "bg-danger-bg text-danger",
  info: "bg-info-bg text-info",
  muted: "bg-surface-2 text-ink-faint",
} as const;

export function Badge({
  tone,
  children,
}: {
  tone: keyof typeof TONES;
  children: React.ReactNode;
}) {
  return (
    <span
      className={`inline-flex items-center gap-1 rounded-full px-2.5 py-1 text-[11px] font-bold ${TONES[tone]}`}
    >
      {children}
    </span>
  );
}
