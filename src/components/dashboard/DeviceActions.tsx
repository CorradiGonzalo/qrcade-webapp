"use client";

import { useState, useTransition } from "react";
import {
  togglePauseAction,
  requestRestartAction,
  requestTestDispenseAction,
} from "@/lib/actions/devices";
import type { Device } from "@/lib/supabase/types";

export function DeviceActions({ device }: { device: Device }) {
  const [pending, startTransition] = useTransition();
  const [testSent, setTestSent] = useState(false);

  function runTest() {
    startTransition(async () => {
      await requestTestDispenseAction(device.id);
      setTestSent(true);
      setTimeout(() => setTestSent(false), 4000);
    });
  }

  return (
    <div className="flex items-center gap-2.5">
      <button
        disabled={pending}
        onClick={runTest}
        title="Dispara el mecanismo una vez para probar que anda — no suma ninguna venta"
        className="rounded-[9px] border border-line-soft bg-surface-2 px-4 py-2.5 text-[13px] font-semibold text-ink disabled:opacity-50"
      >
        🧪 Test
      </button>
      {testSent && (
        <span className="text-[11px] text-ok">
          ✓ Enviado — se ejecuta en el próximo check-in de la placa
        </span>
      )}

      <button
        disabled={pending}
        onClick={() => startTransition(() => requestRestartAction(device.id))}
        className="rounded-[9px] border border-line-soft bg-surface-2 px-4 py-2.5 text-[13px] font-semibold text-ink disabled:opacity-50"
      >
        ⟲ Reiniciar
      </button>
      <button
        disabled={pending}
        onClick={() =>
          startTransition(() => togglePauseAction(device.id, !device.is_paused))
        }
        className={`rounded-[9px] border px-4 py-2.5 text-[13px] font-semibold disabled:opacity-50 ${
          device.is_paused
            ? "border-line-soft bg-ok-bg text-ok"
            : "border-[#6b2323] bg-danger-bg text-danger"
        }`}
      >
        {device.is_paused ? "▶ Reanudar máquina" : "⏸ Pausar máquina"}
      </button>
    </div>
  );
}
