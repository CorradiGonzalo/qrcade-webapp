"use client";

import { useActionState, useState } from "react";
import { createUserAction } from "@/lib/actions/admin";

export function CreateUserForm() {
  const [state, formAction, pending] = useActionState(
    createUserAction,
    undefined
  );
  const [formKey, setFormKey] = useState(0);

  return (
    <div className="rounded-2xl border border-line bg-surface p-6">
      <div className="mb-1 text-sm font-bold">Crear usuario</div>
      <p className="mb-4 text-xs text-ink-muted">
        Le asignás un usuario y una contraseña de un solo uso — se la pasás
        vos a mano. En su primer ingreso, la app le va a pedir que la
        cambie.
      </p>

      {state?.ok ? (
        <div className="rounded-lg border border-info-line bg-info-bg p-4">
          <div className="mb-2 text-xs font-semibold text-info">
            Usuario creado ✓
          </div>
          <div className="space-y-1 text-sm">
            <div>
              <span className="text-ink-faint">Usuario: </span>
              <span className="font-mono font-semibold">{state.username}</span>
            </div>
            <div>
              <span className="text-ink-faint">Clave temporal: </span>
              <span className="font-mono font-semibold">
                {state.tempPassword}
              </span>
            </div>
          </div>
          <button
            onClick={() => setFormKey((k) => k + 1)}
            className="mt-4 text-xs font-semibold text-info hover:underline"
          >
            Crear otro usuario
          </button>
        </div>
      ) : (
        <form key={formKey} action={formAction} className="space-y-3">
          <div>
            <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
              Usuario
            </label>
            <input
              name="username"
              required
              minLength={3}
              placeholder="ej: maria_paz"
              className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm outline-none focus:border-brand"
            />
          </div>
          <div>
            <label className="mb-1.5 block text-xs font-semibold text-ink-muted">
              Nombre (opcional)
            </label>
            <input
              name="display_name"
              placeholder="ej: María Paz"
              className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm outline-none focus:border-brand"
            />
          </div>
          {state?.error && (
            <p className="text-xs font-medium text-danger">{state.error}</p>
          )}
          <button
            type="submit"
            disabled={pending}
            className="rounded-lg bg-brand px-4 py-2.5 text-sm font-semibold text-white transition hover:opacity-90 disabled:opacity-60"
          >
            {pending ? "Creando…" : "Crear usuario"}
          </button>
        </form>
      )}
    </div>
  );
}
