"use client";

import { useState, useTransition } from "react";
import { giveFreeFichaAction } from "@/lib/actions/operador";

export function GiveFreeFichaButton() {
  const [pending, startTransition] = useTransition();
  const [ok, setOk] = useState(false);
  const [error, setError] = useState<string | null>(null);

  function darFicha() {
    setError(null);
    setOk(false);
    startTransition(async () => {
      try {
        await giveFreeFichaAction();
        setOk(true);
        setTimeout(() => setOk(false), 5000);
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo dar la ficha.");
      }
    });
  }

  return (
    <div className="flex flex-col items-center gap-4">
      <button
        disabled={pending}
        onClick={darFicha}
        className="flex h-48 w-48 flex-col items-center justify-center gap-2 rounded-full bg-brand text-white shadow-2xl shadow-brand/40 transition active:scale-95 disabled:opacity-60"
      >
        <span className="text-4xl">🎁</span>
        <span className="text-lg font-bold">
          {pending ? "Enviando…" : "Ficha gratis"}
        </span>
      </button>

      {ok && (
        <p className="text-sm font-semibold text-ok">
          ✓ Ficha enviada — se entrega en el próximo chequeo de la máquina
        </p>
      )}
      {error && <p className="text-sm font-medium text-danger">{error}</p>}
    </div>
  );
}
