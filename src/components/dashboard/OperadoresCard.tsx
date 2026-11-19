"use client";

import { useState, useTransition } from "react";
import {
  createOperatorAction,
  setOperatorActiveAction,
} from "@/lib/actions/devices";
import type { Profile } from "@/lib/supabase/types";

type Operador = Pick<
  Profile,
  "id" | "username" | "display_name" | "operator_active" | "created_at"
>;

/**
 * "Crear operador" — ver migración 0020. Lo arma el DUEÑO, sin que
 * intervenga ningún admin: le da un usuario/contraseña a alguien
 * (ej. un empleado) para que pueda entrar SÓLO a una pantalla mínima
 * con el botón "Ficha gratis" de esta máquina puntual, nada más.
 */
export function OperadoresCard({
  deviceId,
  operadoresIniciales,
}: {
  deviceId: string;
  operadoresIniciales: Operador[];
}) {
  const [operadores, setOperadores] = useState(operadoresIniciales);
  const [pending, startTransition] = useTransition();
  const [error, setError] = useState<string | null>(null);
  const [creado, setCreado] = useState<{ username: string } | null>(null);
  const [formKey, setFormKey] = useState(0);

  function crear(formData: FormData) {
    setError(null);
    const username = String(formData.get("username") || "");
    const displayName = String(formData.get("display_name") || "");
    const password = String(formData.get("password") || "");
    startTransition(async () => {
      try {
        const resultado = await createOperatorAction(deviceId, username, displayName, password);
        setCreado(resultado);
        setOperadores((prev) => [
          ...prev,
          {
            id: crypto.randomUUID(), // placeholder sólo visual -- se reemplaza al recargar la página
            username: resultado.username,
            display_name: displayName || null,
            operator_active: true,
            created_at: new Date().toISOString(),
          },
        ]);
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo crear el operador.");
      }
    });
  }

  function toggleActivo(op: Operador) {
    startTransition(async () => {
      try {
        await setOperatorActiveAction(op.id, deviceId, !op.operator_active);
        setOperadores((prev) =>
          prev.map((o) => (o.id === op.id ? { ...o, operator_active: !o.operator_active } : o))
        );
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo actualizar.");
      }
    });
  }

  return (
    <div className="rounded-2xl border border-line bg-surface p-5">
      <div className="mb-1 text-sm font-bold">Operadores</div>
      <p className="mb-3 text-xs text-ink-muted">
        Cuentas que sólo pueden dar una ficha gratis en esta máquina (ej.
        para un empleado) — no ven ninguna configuración, ni esta ni la
        de ninguna otra máquina.
      </p>

      {operadores.length > 0 && (
        <div className="mb-4 space-y-2">
          {operadores.map((op) => (
            <div
              key={op.id}
              className="flex items-center justify-between gap-2 rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5"
            >
              <div className="min-w-0">
                <div className="truncate text-[13px] font-semibold">
                  {op.display_name || op.username}
                </div>
                <div className="truncate font-mono text-[11px] text-ink-faint">
                  {op.username}
                </div>
              </div>
              <button
                disabled={pending}
                onClick={() => toggleActivo(op)}
                className={`shrink-0 rounded-md border px-2.5 py-1.5 text-[11px] font-semibold disabled:opacity-50 ${
                  op.operator_active
                    ? "border-[#6b2323] bg-danger-bg text-danger"
                    : "border-line-soft bg-ok-bg text-ok"
                }`}
              >
                {op.operator_active ? "Desactivar" : "Reactivar"}
              </button>
            </div>
          ))}
        </div>
      )}

      {creado ? (
        <div className="rounded-lg border border-info-line bg-info-bg p-4">
          <div className="mb-2 text-xs font-semibold text-info">Operador creado ✓</div>
          <div className="text-sm">
            <span className="text-ink-faint">Usuario: </span>
            <span className="font-mono font-semibold">{creado.username}</span>
          </div>
          <p className="mt-2 text-[11px] text-ink-faint">
            Pasale el usuario y la clave que elegiste — él no puede
            cambiarla, así que si la olvidás la tenés que recrear acá
            (desactivando esta cuenta y creando una nueva).
          </p>
          <button
            onClick={() => {
              setCreado(null);
              setFormKey((k) => k + 1);
            }}
            className="mt-3 text-xs font-semibold text-info hover:underline"
          >
            Crear otro operador
          </button>
        </div>
      ) : (
        <form key={formKey} action={crear} className="space-y-2.5">
          <input
            name="username"
            required
            minLength={3}
            placeholder="Usuario (ej: kevin)"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm outline-none focus:border-brand"
          />
          <input
            name="display_name"
            placeholder="Nombre (opcional, ej: Kevin)"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm outline-none focus:border-brand"
          />
          <input
            name="password"
            required
            minLength={6}
            placeholder="Clave (mínimo 6 caracteres)"
            className="w-full rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-sm outline-none focus:border-brand"
          />
          <p className="text-[11px] text-ink-faint">
            Esta clave la elegís vos. El operador no la puede cambiar.
          </p>
          {error && <p className="text-xs font-medium text-danger">{error}</p>}
          <button
            type="submit"
            disabled={pending}
            className="rounded-lg bg-brand px-4 py-2.5 text-sm font-semibold text-white transition hover:opacity-90 disabled:opacity-60"
          >
            {pending ? "Creando…" : "Crear operador"}
          </button>
        </form>
      )}
    </div>
  );
}
