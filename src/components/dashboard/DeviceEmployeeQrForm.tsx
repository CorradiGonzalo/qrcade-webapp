"use client";

import { useActionState, useState } from "react";
import { saveDeviceEmployeeBillingAction } from "@/lib/actions/devices";
import type { Device } from "@/lib/supabase/types";

/**
 * "QR de empleado" — sólo cafeteras. Muestra el QR principal tal como
 * está siempre; esto agrega un SEGUNDO QR, de precio más bajo, que la
 * pantalla destapa con un PIN (ver migración 0015 y el firmware de
 * display). Nunca pide el local: siempre usa el mismo que ya tiene el
 * QR principal — por eso este formulario ni siquiera tiene un campo
 * para eso.
 */
export function DeviceEmployeeQrForm({ device }: { device: Device }) {
  const [state, formAction, pending] = useActionState(
    saveDeviceEmployeeBillingAction,
    undefined
  );
  const [pinVisible, setPinVisible] = useState(false);

  const localListoParaEmpleado = Boolean(device.store_id && device.local_name);

  return (
    <div className="rounded-2xl border border-line bg-surface p-6">
      <div className="mb-1 text-sm font-bold">QR oferta</div>
      <p className="mb-4 text-xs text-ink-muted">
        Un segundo QR, con su propio precio, para que lo use el personal.
        Usa el mismo local que el QR principal ({device.local_name ?? "—"}) —
        acá sólo configurás el nombre de su caja, el monto y el PIN que la
        pantalla va a pedir para destaparlo durante 30 segundos.
      </p>

      {!localListoParaEmpleado && (
        <div className="mb-4 rounded-lg border border-warn bg-warn-bg px-4 py-3 text-xs text-warn">
          ⚠ Primero guardá el QR principal (local y caja) más arriba — el
          QR oferta reusa ese mismo local y todavía no está
          configurado.
        </div>
      )}

      <form action={formAction} className="space-y-4">
        <input type="hidden" name="device_id" value={device.id} />

        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Nombre de la caja del QR oferta
          </label>
          <input
            name="empleado_caja_name"
            defaultValue={device.empleado_caja_name ?? ""}
            placeholder="ej: Garra-1-Oferta"
            disabled={!localListoParaEmpleado}
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand disabled:opacity-50"
          />
        </div>

        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Monto fijo (oferta)
          </label>
          <div className="flex items-center rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3">
            <span className="mr-1.5 text-sm text-info">$</span>
            <input
              name="empleado_mp_monto_fijo"
              type="number"
              step="0.01"
              min="0"
              defaultValue={device.empleado_mp_monto_fijo ?? ""}
              placeholder="500"
              disabled={!localListoParaEmpleado}
              className="w-full bg-transparent text-sm font-semibold text-ink outline-none disabled:opacity-50"
            />
          </div>
        </div>

        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            PIN para destapar el QR (4 a 6 dígitos)
          </label>
          <div className="flex items-center gap-2">
            <input
              name="empleado_pin"
              type={pinVisible ? "text" : "password"}
              inputMode="numeric"
              pattern="\d{4,6}"
              maxLength={6}
              defaultValue={device.empleado_pin ?? ""}
              placeholder="1234"
              disabled={!localListoParaEmpleado}
              className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm font-semibold tracking-widest text-ink outline-none focus:border-brand disabled:opacity-50"
            />
            <button
              type="button"
              onClick={() => setPinVisible((v) => !v)}
              className="shrink-0 rounded-lg border border-line-soft px-3 py-3 text-xs font-semibold text-ink-muted"
            >
              {pinVisible ? "Ocultar" : "Ver"}
            </button>
          </div>
          <p className="mt-1.5 text-[11px] text-ink-faint">
            En la pantalla de la cafetera: tocar en cualquier lugar,
            escribir este PIN, y se muestra el QR oferta por 30
            segundos. 3 intentos fallidos bloquean el teclado por un
            rato.
          </p>
        </div>

        {state?.error && (
          <p className="text-xs font-medium text-danger">{state.error}</p>
        )}
        {state?.ok && (
          <p className="text-xs font-medium text-ok">
            QR oferta guardado y aprovisionado ✓
          </p>
        )}

        <button
          type="submit"
          disabled={pending || !localListoParaEmpleado}
          className="w-full rounded-lg border border-line-soft bg-surface-2 py-3 text-sm font-semibold text-ink transition hover:opacity-90 disabled:opacity-60"
        >
          {pending ? "Aprovisionando…" : "Guardar QR oferta"}
        </button>
      </form>
    </div>
  );
}
