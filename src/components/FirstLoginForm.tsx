"use client";

import { useActionState } from "react";
import { changePasswordAction } from "@/lib/actions/auth";

export function FirstLoginForm({ displayName }: { displayName: string }) {
  const [state, formAction, pending] = useActionState(
    changePasswordAction,
    undefined
  );

  return (
    <div className="w-[460px] rounded-[20px] border border-line-soft bg-surface p-9 shadow-2xl">
      <div className="mb-4 flex items-center gap-2.5">
        <div className="flex h-[30px] w-[30px] items-center justify-center rounded-lg bg-brand">
          <div className="h-3 w-3 rounded-sm border-2 border-[#dbe6ff]" />
        </div>
        <div className="text-base font-bold text-ink">
          Bienvenido{displayName ? `, ${displayName}` : ""}
        </div>
      </div>
      <p className="mb-6 text-sm leading-relaxed text-ink-muted">
        Es tu primer ingreso. Por seguridad, elegí una contraseña nueva y
        dejanos un mail de recuperación por si la olvidás.
      </p>

      <form action={formAction} className="space-y-4">
        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Nueva contraseña
          </label>
          <input
            name="password"
            type="password"
            required
            minLength={8}
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
          />
        </div>

        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Repetir contraseña
          </label>
          <input
            name="confirm"
            type="password"
            required
            minLength={8}
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
          />
        </div>

        <div>
          <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
            Mail de recuperación
          </label>
          <input
            name="recovery_email"
            type="email"
            required
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
          />
        </div>

        {state?.error && (
          <p className="text-xs font-medium text-danger">{state.error}</p>
        )}

        <button
          type="submit"
          disabled={pending}
          className="w-full rounded-lg bg-brand py-3 text-sm font-semibold text-white shadow-lg shadow-brand/30 transition hover:opacity-90 disabled:opacity-60"
        >
          {pending ? "Guardando…" : "Guardar y continuar"}
        </button>
      </form>

      <p className="mt-4 text-center text-[11px] text-ink-faint">
        No vas a poder usar la app hasta completar este paso
      </p>
    </div>
  );
}
