"use client";

import { useActionState } from "react";
import { loginAction } from "@/lib/actions/auth";

export function LoginForm() {
  const [state, formAction, pending] = useActionState(loginAction, undefined);

  return (
    <form action={formAction} className="space-y-4">
      <div>
        <label
          htmlFor="username"
          className="mb-1.5 block text-xs font-semibold text-ink-muted"
        >
          Usuario
        </label>
        <input
          id="username"
          name="username"
          type="text"
          autoComplete="username"
          required
          className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 text-sm text-ink outline-none focus:border-brand"
        />
      </div>

      <div>
        <label
          htmlFor="password"
          className="mb-1.5 block text-xs font-semibold text-ink-muted"
        >
          Contraseña
        </label>
        <input
          id="password"
          name="password"
          type="password"
          autoComplete="current-password"
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
        {pending ? "Ingresando…" : "Iniciar sesión"}
      </button>

      <p className="text-center text-[11px] text-ink-faint">
        ¿Olvidaste tu contraseña? Pedísela al admin
      </p>
    </form>
  );
}
