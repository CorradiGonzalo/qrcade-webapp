"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { logoutAction } from "@/lib/actions/auth";

const LINKS = [
  { href: "/admin/equipos", label: "Equipos", icon: "🔒" },
  { href: "/admin/firmware", label: "Firmware", icon: "📦" },
  { href: "/admin/usuarios", label: "Usuarios", icon: "👥" },
];

export function AdminSidebar({
  displayName,
}: {
  displayName: string;
}) {
  const pathname = usePathname();

  return (
    <div className="flex w-[236px] shrink-0 flex-col border-r border-line bg-sidebar p-[18px] pt-6">
      <div className="mb-9 flex items-center gap-2.5 px-1.5">
        <div className="flex h-[30px] w-[30px] items-center justify-center rounded-lg bg-brand">
          <div className="h-3 w-3 rounded-sm border-2 border-[#dbe6ff]" />
        </div>
        <div className="text-[17px] font-bold tracking-tight">QRcade</div>
      </div>

      <div className="mb-2 px-2.5 text-[11px] font-bold tracking-wide text-ink-faint">
        CONTROL
      </div>
      <nav className="mb-5 space-y-1">
        {LINKS.map((link) => {
          const active = pathname.startsWith(link.href);
          return (
            <Link
              key={link.href}
              href={link.href}
              className={`block rounded-lg px-3 py-2.5 text-[13px] font-semibold transition ${
                active
                  ? "bg-brand-soft text-ink"
                  : "text-ink-muted hover:text-ink"
              }`}
            >
              {link.icon} {link.label}
            </Link>
          );
        })}
      </nav>

      <div className="mt-auto rounded-[10px] border border-line-soft bg-surface-2 p-3">
        <div className="text-xs font-semibold text-ink">{displayName}</div>
        <div className="mb-2 text-[11px] text-ink-faint">Administrador</div>
        <form action={logoutAction}>
          <button
            type="submit"
            className="text-[11px] font-semibold text-ink-faint hover:text-ink"
          >
            Cerrar sesión
          </button>
        </form>
      </div>
    </div>
  );
}
