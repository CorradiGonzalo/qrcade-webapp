const COLORS = {
  ok: "bg-ok",
  warn: "bg-warn",
  danger: "bg-danger",
  muted: "bg-ink-faint",
} as const;

export function StatusDot({
  tone,
  label,
}: {
  tone: keyof typeof COLORS;
  label: string;
}) {
  return (
    <span
      title={label}
      aria-label={label}
      className={`inline-block h-[9px] w-[9px] rounded-full ${COLORS[tone]}`}
    />
  );
}
