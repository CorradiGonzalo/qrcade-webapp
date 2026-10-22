"use client";

import { useTransition } from "react";
import { cancelarActualizacionPendienteAction } from "@/lib/actions/admin";

export function CancelUpdateButton({
  deviceId,
  firmwareVersionId,
}: {
  deviceId: string;
  firmwareVersionId: string;
}) {
  const [pending, startTransition] = useTransition();

  return (
    <button
      type="button"
      disabled={pending}
      onClick={() =>
        startTransition(() =>
          cancelarActualizacionPendienteAction(deviceId, firmwareVersionId)
        )
      }
      title="La placa ya está en la versión nueva pero el backend sigue insistiendo — esto le dice que pare."
      className="ml-2 text-[11px] font-semibold text-danger hover:opacity-80 disabled:opacity-50"
    >
      {pending ? "Cancelando…" : "Cancelar reintentos"}
    </button>
  );
}
