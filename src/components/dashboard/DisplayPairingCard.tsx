"use client";

import { useState, useTransition } from "react";
import { generateDisplayPairingCodeAction } from "@/lib/actions/devices";
import type { Device } from "@/lib/supabase/types";

/**
 * Sólo para cafeteras que usan una SEGUNDA ESP32 (sin relé) para
 * mostrar el QR — caso puntual de las primeras máquinas, armadas con
 * una placa soldada sin pantalla (ver migración 0014). No tiene nada
 * que ver con el firmware que cobra: esto sólo genera un código para
 * que esa otra placa pueda pedir el QR vigente sin ser tratada como
 * una máquina nueva.
 */
export function DisplayPairingCard({ device }: { device: Device }) {
  const [pending, startTransition] = useTransition();
  const [code, setCode] = useState(device.display_pairing_code);
  const [error, setError] = useState<string | null>(null);
  const [copiado, setCopiado] = useState(false);

  function generar() {
    setError(null);
    startTransition(async () => {
      try {
        const result = await generateDisplayPairingCodeAction(device.id);
        setCode(result.code);
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo generar el código.");
      }
    });
  }

  async function copiar() {
    if (!code) return;
    try {
      await navigator.clipboard.writeText(code);
      setCopiado(true);
      setTimeout(() => setCopiado(false), 2500);
    } catch {
      // Sin acceso al clipboard (http, permisos, etc.) — el código ya
      // está visible en pantalla, el usuario lo copia a mano.
    }
  }

  return (
    <div className="mt-4 rounded-2xl border border-line bg-surface p-5">
      <div className="mb-1.5 text-sm font-bold">Pantalla externa (opcional)</div>
      <p className="mb-4 text-xs text-ink-muted">
        Sólo si esta cafetera usa una ESP32 separada únicamente para
        mostrar el QR (sin relé). Cargá este código en el portal WiFi de
        esa placa — no sirve para nada más, no reemplaza al código de
        vinculación de la placa que cobra.
      </p>

      {code ? (
        <div className="flex items-center gap-2.5">
          <code className="flex-1 rounded-lg border border-line-soft bg-surface-2 px-3.5 py-2.5 text-center font-mono text-sm tracking-wider text-ink">
            {code}
          </code>
          <button
            type="button"
            onClick={copiar}
            className="rounded-[9px] border border-line-soft bg-surface-2 px-3.5 py-2.5 text-[13px] font-semibold text-ink"
          >
            {copiado ? "✓" : "Copiar"}
          </button>
        </div>
      ) : (
        <p className="mb-3 text-xs text-ink-faint">
          Todavía no generaste un código para esta máquina.
        </p>
      )}

      {error && <p className="mt-2 text-xs font-medium text-danger">{error}</p>}

      <button
        type="button"
        disabled={pending}
        onClick={generar}
        className="mt-3 rounded-[9px] border border-line-soft bg-surface-2 px-4 py-2.5 text-[13px] font-semibold text-ink disabled:opacity-50"
      >
        {pending ? "Generando…" : code ? "Regenerar código" : "Generar código"}
      </button>
      {code && (
        <p className="mt-2 text-[11px] text-ink-faint">
          Regenerarlo invalida el anterior — la pantalla que lo tenía
          cargado dejaría de poder leer el QR hasta que le cargues el
          nuevo.
        </p>
      )}
    </div>
  );
}
