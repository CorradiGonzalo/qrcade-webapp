"use client";

import { useState, useTransition } from "react";
import { StatusDot } from "@/components/StatusDot";
import {
  revokeDeviceAction,
  reactivateDeviceAction,
  regenerateDeviceCodeAction,
} from "@/lib/actions/admin";
import type { Device } from "@/lib/supabase/types";

const STATUS_META = {
  claimed: { tone: "ok" as const, label: "Reclamada" },
  unclaimed: { tone: "warn" as const, label: "Sin reclamar" },
  revoked: { tone: "danger" as const, label: "Revocada" },
};

export function DeviceStatusRow({
  device,
  ownerName,
}: {
  device: Device;
  ownerName: string | null;
}) {
  const [pending, startTransition] = useTransition();
  const [copied, setCopied] = useState(false);
  const meta = STATUS_META[device.status];

  async function copyCode() {
    if (!device.claim_code) return;
    try {
      await navigator.clipboard.writeText(device.claim_code);
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {
      // clipboard no disponible (http sin permisos, etc.) — no hace falta
      // romper nada, el código sigue visible para copiar a mano.
    }
  }

  function regenerateCode() {
    const message =
      device.status === "claimed"
        ? `Esta placa está a nombre de ${ownerName ?? "un usuario"}. Regenerar el código la desvincula de esa cuenta y borra su configuración de cobro (Access Token, caja, monto) para que el próximo dueño cargue la suya. ¿Confirmás?`
        : "Se va a generar un código nuevo para esta placa. ¿Confirmás?";

    if (!window.confirm(message)) return;
    startTransition(() => regenerateDeviceCodeAction(device.id));
  }

  return (
    <div
      className={`grid grid-cols-[2.2fr_1.6fr_1.2fr_0.5fr_1.4fr] items-center rounded-[10px] border p-4 ${
        device.status === "revoked" ? "border-[#6b2323]" : "border-line"
      }`}
    >
      <div>
        <div className="font-mono text-[13px] font-semibold">{device.mac}</div>
        {device.status === "unclaimed" && device.claim_code && (
          <button
            onClick={copyCode}
            className="mt-1 inline-flex items-center gap-1.5 rounded-md bg-warn-bg px-2 py-0.5 font-mono text-[11px] font-bold tracking-wider text-warn"
            title="Copiar código para pasárselo al usuario"
          >
            {device.claim_code}
            <span className="text-[10px] font-normal">
              {copied ? "✓ copiado" : "copiar"}
            </span>
          </button>
        )}
      </div>
      <div className="text-xs text-ink-muted">
        {ownerName ?? <span className="italic text-ink-faint">—</span>}
      </div>
      <div className="text-xs text-ink-faint">
        {new Date(device.created_at).toLocaleDateString("es-AR")}
      </div>
      <div className="flex justify-center">
        <StatusDot tone={meta.tone} label={meta.label} />
      </div>
      <div className="flex justify-end gap-3 text-right">
        {device.status !== "revoked" && (
          <button
            disabled={pending}
            onClick={regenerateCode}
            className="text-xs font-semibold text-ink-faint hover:text-brand disabled:opacity-50"
          >
            {device.status === "claimed" ? "Reasignar" : "Nuevo código"}
          </button>
        )}
        {device.status === "revoked" ? (
          <button
            disabled={pending}
            onClick={() =>
              startTransition(() => reactivateDeviceAction(device.id))
            }
            className="text-xs font-semibold text-ink-faint hover:text-ink disabled:opacity-50"
          >
            Reactivar
          </button>
        ) : (
          <button
            disabled={pending}
            onClick={() => startTransition(() => revokeDeviceAction(device.id))}
            className="text-xs font-semibold text-ink-faint hover:text-danger disabled:opacity-50"
          >
            Revocar
          </button>
        )}
      </div>
    </div>
  );
}
