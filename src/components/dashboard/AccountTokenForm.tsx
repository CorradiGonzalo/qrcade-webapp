"use client";

import { useActionState } from "react";
import { saveAccountTokenAction } from "@/lib/actions/devices";

export function AccountTokenForm({
  currentToken,
}: {
  currentToken: string | null;
}) {
  const [state, formAction, pending] = useActionState(
    saveAccountTokenAction,
    undefined
  );

  return (
    <form action={formAction} className="space-y-5">
      <div className="rounded-2xl border border-line bg-surface p-6">
        <div className="mb-1 text-sm font-bold">Cuenta de Mercado Pago</div>
        <p className="mb-4 text-xs text-ink-muted">
          Un solo Access Token para toda tu cuenta — vale para todas tus
          máquinas. El dinero de cada cobro va directo a esta cuenta de MP.
          Después, en cada máquina, sólo elegís el local, la caja y cómo
          cobra.
        </p>
        <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
          Access Token
        </label>
        <input
          name="mp_access_token"
          defaultValue={currentToken ?? ""}
          placeholder="APP_USR-..."
          className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 font-mono text-[13px] text-ink outline-none focus:border-brand"
        />
        {currentToken && (
          <p className="mt-2 text-[11px] text-ok">
            ✓ Token cargado y validado contra Mercado Pago.
          </p>
        )}
      </div>

      {state?.error && (
        <p className="text-xs font-medium text-danger">{state.error}</p>
      )}
      {state?.ok && <p className="text-xs font-medium text-ok">Guardado ✓</p>}

      <button
        type="submit"
        disabled={pending}
        className="w-full rounded-lg bg-brand py-3 text-sm font-semibold text-white transition hover:opacity-90 disabled:opacity-60"
      >
        {pending ? "Validando…" : "Guardar token"}
      </button>

      <div className="rounded-lg border border-info-line bg-info-bg px-4 py-4 text-xs text-info">
        💡 Para que los pagos se acrediten solos, configurá esta misma URL
        como webhook en tu cuenta de Mercado Pago (Tu negocio → Webhooks →
        Notificaciones de pago).
      </div>
    </form>
  );
}
