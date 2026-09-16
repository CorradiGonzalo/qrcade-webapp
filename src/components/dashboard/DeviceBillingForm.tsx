"use client";

import { useActionState, useState, useTransition } from "react";
import {
  addComboAction,
  removeComboAction,
  saveDeviceBillingAction,
} from "@/lib/actions/devices";
import type { Device, DeviceFichaCombo } from "@/lib/supabase/types";

export function DeviceBillingForm({
  device,
  combos,
  hasAccountToken,
}: {
  device: Device;
  combos: DeviceFichaCombo[];
  hasAccountToken: boolean;
}) {
  const [state, formAction, pending] = useActionState(
    saveDeviceBillingAction,
    undefined
  );
  const [mode, setMode] = useState<"fijo" | "combo">(device.mode);

  return (
    <div className="space-y-5">
      {!hasAccountToken && (
        <div className="rounded-lg border border-warn bg-warn-bg px-4 py-4 text-xs text-warn">
          ⚠ Todavía no cargaste el Access Token de tu cuenta en{" "}
          <a href="/dashboard/cuenta" className="font-semibold underline">
            Mi cuenta
          </a>
          . Hacelo primero — sin token no se puede aprovisionar esta máquina
          en Mercado Pago.
        </div>
      )}

      <form action={formAction} className="space-y-5">
        <input type="hidden" name="device_id" value={device.id} />
        <input type="hidden" name="mode" value={mode} />

        <div className="rounded-2xl border border-line bg-surface p-6">
          <div className="mb-1 text-sm font-bold">Local y caja</div>
          <p className="mb-4 text-xs text-ink-muted">
            Si el local ya existe en tu cuenta de MP se reutiliza; si no,
            se crea. La caja siempre es nueva por máquina.
          </p>

          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Nombre del local
          </label>
          <input
            name="local_name"
            defaultValue={device.local_name ?? ""}
            placeholder="ej: Shopping Norte"
            className="mb-[18px] w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
          />

          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Nombre de la caja
          </label>
          <input
            name="caja_name"
            defaultValue={device.caja_name ?? ""}
            placeholder="ej: Garra-1"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
          />
        </div>

        <div className="rounded-2xl border border-line bg-surface p-6">
          <div className="mb-1 text-sm font-bold">Cómo cobra</div>
          <p className="mb-4 text-xs text-ink-muted">
            Monto fijo: el QR queda bloqueado a un precio — 1 pago = 1
            ficha. Fichas por monto abierto: el QR acepta cualquier monto,
            y vos definís qué monto exacto da cuántas fichas.
          </p>

          <div className="mb-4 flex gap-2">
            <button
              type="button"
              onClick={() => setMode("fijo")}
              className={`flex-1 rounded-lg border px-4 py-2.5 text-xs font-semibold transition ${
                mode === "fijo"
                  ? "border-brand bg-brand-soft text-ink"
                  : "border-line-soft bg-surface-2 text-ink-muted"
              }`}
            >
              Monto fijo
            </button>
            <button
              type="button"
              onClick={() => setMode("combo")}
              className={`flex-1 rounded-lg border px-4 py-2.5 text-xs font-semibold transition ${
                mode === "combo"
                  ? "border-brand bg-brand-soft text-ink"
                  : "border-line-soft bg-surface-2 text-ink-muted"
              }`}
            >
              Fichas por monto abierto
            </button>
          </div>

          {mode === "fijo" && (
            <>
              <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
                Monto fijo por juego
              </label>
              <div className="flex items-center rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3">
                <span className="mr-1.5 text-sm text-info">$</span>
                <input
                  name="mp_monto_fijo"
                  type="number"
                  step="0.01"
                  min="0"
                  defaultValue={device.mp_monto_fijo ?? ""}
                  placeholder="1000"
                  className="w-full bg-transparent text-sm font-semibold text-ink outline-none"
                />
              </div>
            </>
          )}

          {mode === "combo" && (
            <p className="text-xs text-ink-muted">
              Los combos se configuran abajo, una vez que guardes esta
              máquina en modo &quot;fichas por monto abierto&quot;.
            </p>
          )}
        </div>

        {state?.error && (
          <p className="text-xs font-medium text-danger">{state.error}</p>
        )}
        {state?.ok && (
          <p className="text-xs font-medium text-ok">
            Guardado y aprovisionado en Mercado Pago ✓
          </p>
        )}

        <button
          type="submit"
          disabled={pending || !hasAccountToken}
          className="w-full rounded-lg bg-brand py-3 text-sm font-semibold text-white transition hover:opacity-90 disabled:opacity-60"
        >
          {pending ? "Aprovisionando en Mercado Pago…" : "Guardar cambios"}
        </button>
      </form>

      {device.mode === "combo" && device.provisioned_at && (
        <ComboManager deviceId={device.id} combos={combos} />
      )}

      <div className="rounded-lg border border-info-line bg-info-bg px-4 py-4 text-xs text-info">
        💡 La red WiFi de la máquina se configura directo desde la placa (red
        &quot;QRcade_Config&quot;). Acá sólo se maneja lo relacionado a
        Mercado Pago.
      </div>
    </div>
  );
}

function ComboManager({
  deviceId,
  combos,
}: {
  deviceId: string;
  combos: DeviceFichaCombo[];
}) {
  const [pending, startTransition] = useTransition();
  const [fichas, setFichas] = useState("");
  const [monto, setMonto] = useState("");
  const [error, setError] = useState<string | null>(null);

  function agregar() {
    setError(null);
    const f = Number(fichas);
    const m = Number(monto);
    if (!Number.isInteger(f) || f <= 0) {
      setError("Las fichas tienen que ser un entero mayor a 0.");
      return;
    }
    if (!(m > 0)) {
      setError("El monto tiene que ser mayor a 0.");
      return;
    }
    startTransition(async () => {
      try {
        await addComboAction(deviceId, f, m);
        setFichas("");
        setMonto("");
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo guardar.");
      }
    });
  }

  return (
    <div className="rounded-2xl border border-line bg-surface p-6">
      <div className="mb-1 text-sm font-bold">Combos de fichas</div>
      <p className="mb-4 text-xs text-ink-muted">
        Si el pago coincide EXACTO con el monto, se acreditan esas fichas.
        Sin coincidencia exacta no se acredita nada (eso lo arreglás
        directo con el cliente).
      </p>

      {combos.length > 0 && (
        <div className="mb-4 space-y-2">
          {combos
            .slice()
            .sort((a, b) => a.monto - b.monto)
            .map((c) => (
              <div
                key={c.id}
                className="flex items-center justify-between rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5"
              >
                <span className="text-sm text-ink">
                  ${c.monto} → {c.fichas} ficha{c.fichas === 1 ? "" : "s"}
                </span>
                <button
                  disabled={pending}
                  onClick={() =>
                    startTransition(() => removeComboAction(c.id, deviceId))
                  }
                  className="text-xs font-semibold text-danger hover:opacity-80 disabled:opacity-50"
                >
                  Quitar
                </button>
              </div>
            ))}
        </div>
      )}

      <div className="flex items-end gap-2.5">
        <div className="flex-1">
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Monto
          </label>
          <input
            type="number"
            step="0.01"
            min="0"
            value={monto}
            onChange={(e) => setMonto(e.target.value)}
            placeholder="1000"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm text-ink outline-none focus:border-brand"
          />
        </div>
        <div className="flex-1">
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Fichas
          </label>
          <input
            type="number"
            min="1"
            step="1"
            value={fichas}
            onChange={(e) => setFichas(e.target.value)}
            placeholder="1"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm text-ink outline-none focus:border-brand"
          />
        </div>
        <button
          type="button"
          disabled={pending}
          onClick={agregar}
          className="rounded-lg border border-line-soft bg-surface-2 px-4 py-2.5 text-xs font-semibold text-ink disabled:opacity-50"
        >
          + Agregar
        </button>
      </div>
      {error && <p className="mt-2 text-xs font-medium text-danger">{error}</p>}
    </div>
  );
}
