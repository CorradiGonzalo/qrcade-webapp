"use client";

import { useRef, useState, useTransition } from "react";
import { renameDeviceAction } from "@/lib/actions/devices";

export function DeviceNameEditor({
  deviceId,
  alias,
  mac,
}: {
  deviceId: string;
  alias: string | null;
  mac: string;
}) {
  const [editing, setEditing] = useState(false);
  const [value, setValue] = useState(alias ?? "");
  const [error, setError] = useState<string | null>(null);
  const [pending, startTransition] = useTransition();
  const inputRef = useRef<HTMLInputElement>(null);

  function openEditor() {
    setValue(alias ?? "");
    setError(null);
    setEditing(true);
    // Esperamos al próximo render para que el input ya exista.
    setTimeout(() => inputRef.current?.focus(), 0);
  }

  function cancel() {
    setEditing(false);
    setError(null);
  }

  function save() {
    const clean = value.trim();
    if (!clean) {
      setError("No puede quedar vacío.");
      return;
    }
    startTransition(async () => {
      try {
        await renameDeviceAction(deviceId, clean);
        setEditing(false);
      } catch (err) {
        setError(err instanceof Error ? err.message : "No se pudo guardar.");
      }
    });
  }

  if (editing) {
    return (
      <div className="mb-1">
        <div className="flex flex-wrap items-center gap-2">
          <input
            ref={inputRef}
            value={value}
            onChange={(e) => setValue(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter") {
                e.preventDefault();
                save();
              }
              if (e.key === "Escape") cancel();
            }}
            maxLength={60}
            placeholder="Ej: Peluchera Shopping Norte"
            className="w-full max-w-[320px] rounded-lg border border-brand bg-surface-2 px-3 py-1.5 text-xl font-bold tracking-tight text-ink outline-none"
          />
          <button
            type="button"
            disabled={pending}
            onClick={save}
            className="rounded-lg bg-brand px-3 py-1.5 text-[13px] font-semibold text-white disabled:opacity-50"
          >
            {pending ? "Guardando…" : "Guardar"}
          </button>
          <button
            type="button"
            disabled={pending}
            onClick={cancel}
            className="rounded-lg border border-line-soft bg-surface-2 px-3 py-1.5 text-[13px] font-semibold text-ink disabled:opacity-50"
          >
            Cancelar
          </button>
        </div>
        {error && <p className="mt-1.5 text-xs font-medium text-danger">{error}</p>}
      </div>
    );
  }

  return (
    <div className="group mb-1 flex items-center gap-2">
      <h1 className="text-2xl font-bold tracking-tight">{alias ?? mac}</h1>
      <button
        type="button"
        onClick={openEditor}
        title="Editar nombre"
        aria-label="Editar nombre"
        className="rounded-md p-1 text-ink-faint opacity-60 transition hover:bg-surface-2 hover:text-ink hover:opacity-100"
      >
        ✏️
      </button>
    </div>
  );
}
