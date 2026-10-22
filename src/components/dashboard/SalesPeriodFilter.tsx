"use client";

import { useRouter, useSearchParams, usePathname } from "next/navigation";
import { useState } from "react";

const PRESETS: { key: string; label: string }[] = [
  { key: "hoy", label: "Hoy" },
  { key: "7d", label: "7 días" },
  { key: "30d", label: "30 días" },
  { key: "mes", label: "Este mes" },
  { key: "todo", label: "Todo" },
];

/**
 * Filtro de período para la sección de Ventas del detalle de una máquina.
 * Todo vive en la URL (?periodo=... o ?desde=...&hasta=...) — así el
 * filtro sobrevive a un refresh y se puede compartir/linkear. La data en
 * sí la trae el server component de la página, este componente sólo
 * arma la navegación.
 */
export function SalesPeriodFilter({ periodoActivo }: { periodoActivo: string }) {
  const router = useRouter();
  const pathname = usePathname();
  const searchParams = useSearchParams();

  const esCustom = periodoActivo === "custom";
  const [desde, setDesde] = useState(searchParams.get("desde") ?? "");
  const [hasta, setHasta] = useState(searchParams.get("hasta") ?? "");
  const [mostrarCustom, setMostrarCustom] = useState(esCustom);

  function irAPreset(key: string) {
    setMostrarCustom(false);
    router.push(`${pathname}?periodo=${key}`, { scroll: false });
  }

  function aplicarCustom() {
    if (!desde || !hasta) return;
    router.push(
      `${pathname}?periodo=custom&desde=${desde}&hasta=${hasta}`,
      { scroll: false }
    );
  }

  return (
    <div className="mb-4 flex flex-wrap items-center gap-1.5">
      {PRESETS.map((p) => (
        <button
          key={p.key}
          type="button"
          onClick={() => irAPreset(p.key)}
          className={`rounded-full px-3 py-1.5 text-[12px] font-semibold transition ${
            periodoActivo === p.key
              ? "bg-brand text-white"
              : "border border-line-soft bg-surface-2 text-ink-muted hover:text-ink"
          }`}
        >
          {p.label}
        </button>
      ))}
      <button
        type="button"
        onClick={() => setMostrarCustom((v) => !v)}
        className={`rounded-full px-3 py-1.5 text-[12px] font-semibold transition ${
          esCustom
            ? "bg-brand text-white"
            : "border border-line-soft bg-surface-2 text-ink-muted hover:text-ink"
        }`}
      >
        Personalizado
      </button>

      {mostrarCustom && (
        <div className="mt-1 flex w-full flex-wrap items-center gap-2">
          <input
            type="date"
            value={desde}
            onChange={(e) => setDesde(e.target.value)}
            className="rounded-lg border border-line-soft bg-surface-2 px-2.5 py-1.5 text-[12px] text-ink outline-none focus:border-brand"
          />
          <span className="text-[12px] text-ink-faint">a</span>
          <input
            type="date"
            value={hasta}
            onChange={(e) => setHasta(e.target.value)}
            className="rounded-lg border border-line-soft bg-surface-2 px-2.5 py-1.5 text-[12px] text-ink outline-none focus:border-brand"
          />
          <button
            type="button"
            disabled={!desde || !hasta}
            onClick={aplicarCustom}
            className="rounded-lg bg-brand px-3 py-1.5 text-[12px] font-semibold text-white disabled:opacity-50"
          >
            Aplicar
          </button>
        </div>
      )}
    </div>
  );
}
