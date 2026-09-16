export function Card({
  children,
  className = "",
  highlight,
}: {
  children: React.ReactNode;
  className?: string;
  highlight?: boolean;
}) {
  return (
    <div
      className={`rounded-2xl border bg-surface p-5 ${
        highlight ? "border-brand" : "border-line"
      } ${className}`}
    >
      {children}
    </div>
  );
}
