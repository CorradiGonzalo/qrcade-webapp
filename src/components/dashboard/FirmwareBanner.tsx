"use client";

import { useTransition } from "react";
import { respondFirmwareAction } from "@/lib/actions/devices";

export function FirmwareBanner({
  deviceId,
  deviceLabel,
  version,
  changelog,
  firmwareVersionId,
}: {
  deviceId: string;
  deviceLabel: string;
  version: string;
  changelog: string | null;
  firmwareVersionId: string;
}) {
  const [pending, startTransition] = useTransition();

  return (
    <div className="mb-4 flex items-center justify-between rounded-xl border border-info-line bg-info-bg px-5 py-4">
      <div className="flex items-center gap-3">
        <div className="text-xl">🆕</div>
        <div>
          <div className="text-[13px] font-semibold text-[#bcd4f7]">
            Hay firmware nuevo ({version}) para &quot;{deviceLabel}&quot;
          </div>
          {changelog && (
            <div className="mt-0.5 text-xs text-info">{changelog}</div>
          )}
        </div>
      </div>
      <div className="flex gap-2">
        <button
          disabled={pending}
          onClick={() =>
            startTransition(() =>
              respondFirmwareAction(deviceId, firmwareVersionId, "accepted")
            )
          }
          className="rounded-lg bg-brand px-3.5 py-2 text-xs font-semibold text-white disabled:opacity-50"
        >
          Actualizar ahora
        </button>
        <button
          disabled={pending}
          onClick={() =>
            startTransition(() =>
              respondFirmwareAction(deviceId, firmwareVersionId, "dismissed")
            )
          }
          className="rounded-lg border border-[#2a3d5c] px-3.5 py-2 text-xs font-semibold text-info disabled:opacity-50"
        >
          Más tarde
        </button>
      </div>
    </div>
  );
}
