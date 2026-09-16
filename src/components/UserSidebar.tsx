"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { logoutAction } from "@/lib/actions/auth";

export function UserSidebar({ displayName }: { displayName: string }) {
  const pathname = usePathname();
  const activeMaquinas =
    pathname.startsWith("/dashboard") && !pathname.startsWith("/dashboard/cuenta");
  const activeCuenta = pathname.startsWith("/dashboard/cuenta");

  return (
    <div className="flex w-[236px] shrink-0 flex-col border-r border-line bg-sidebar p-[18px] pt-6">
      <div className="mb-9 flex items-center gap-2.5 px-1.5">
        <div className="flex h-[30px] w-[30px] items-center justify-center rounded-lg bg-brand">
          <div className="h-3 w-3 rounded-sm border-2 border-[#dbe6ff]" />
        </div>
        <div className="text-[17px] font-bold tracking-tight">QRcade</div>
      </div>

      <Link
        href="/dashboard"
        className={`mb-1 block rounded-lg px-3 py-2.5 text-[13px] font-semibold transition ${
          activeMaquinas ? "bg-brand-soft text-ink" : "text-ink-muted hover:text-ink"
        }`}
      >
        📊 Mis máquinas
      </Link>

      <Link
        href="/dashboard/cuenta"
        className={`mb-1 block rounded-lg px-3 py-2.5 text-[13px] font-semibold transition ${
          activeCuenta ? "bg-brand-soft text-ink" : "text-ink-muted hover:text-ink"
        }`}
      >
        💳 Mi cuenta
      </Link>

      <div className="mt-auto rounded-[10px] border border-line-soft bg-surface-2 p-3">
        <div className="text-xs font-semibold text-ink">{displayName}</div>
        <div className="mb-2 text-[11px] text-ink-faint">Usuario</div>
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
