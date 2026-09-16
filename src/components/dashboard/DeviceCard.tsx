"use client";

import Link from "next/link";
import { useTransition } from "react";
import { togglePauseAction } from "@/lib/actions/devices";
import type { Device } from "@/lib/supabase/types";

function isOnline(device: Device) {
  if (!device.last_seen_at) return false;
  return Date.now() - new Date(device.last_seen_at).getTime() < 2 * 60 * 1000;
}

export function DeviceCard({ device }: { device: Device }) {
  const [pending, startTransition] = useTransition();
  const online = isOnline(device);

  return (
    <div className="rounded-2xl border border-line bg-surface p-[22px]">
      <div className="mb-4 flex items-start justify-between">
        <div>
          <div className="text-[15px] font-bold">
            {device.alias ?? device.mac}
          </div>
          <div className="mt-1 text-[11px] text-ink-faint">
            {device.last_seen_at
              ? online
                ? "Conectada"
                : `Sin señal desde ${new Date(
                    device.last_seen_at
                  ).toLocaleString("es-AR")}`
              : "Todavía no se conectó"}
          </div>
        </div>
        <span
          className={`rounded-full px-2.5 py-1 text-[11px] font-bold ${
            online ? "bg-ok-bg text-ok" : "bg-danger-bg text-danger"
          }`}
        >
          ● {online ? "Online" : "Offline"}
        </span>
      </div>

      <div className="mb-[18px] flex gap-5">
        <div>
          <div className="text-[11px] text-ink-faint">Ventas</div>
          <div className="text-lg font-bold text-ink-faint">Próximamente</div>
        </div>
      </div>

      <div className="flex gap-2">
        <Link
          href={`/dashboard/maquinas/${device.id}`}
          className="flex-1 rounded-lg bg-brand py-2.5 text-center text-xs font-semibold text-white transition hover:opacity-90"
        >
          Ver detalle
        </Link>
        <Link
          href={`/dashboard/maquinas/${device.id}/cobro`}
          className="flex-1 rounded-lg border border-line-soft bg-surface-2 py-2.5 text-center text-xs font-semibold text-ink"
        >
          ⚙ Cobro
        </Link>
        <button
          disabled={pending}
          onClick={() =>
            startTransition(() => togglePauseAction(device.id, !device.is_paused))
          }
          className={`flex-1 rounded-lg py-2.5 text-xs font-semibold transition disabled:opacity-50 ${
            device.is_paused
              ? "bg-ok-bg text-ok"
              : "bg-danger-bg text-danger"
          }`}
        >
          {device.is_paused ? "Reanudar" : "Pausar"}
        </button>
      </div>
    </div>
  );
}
