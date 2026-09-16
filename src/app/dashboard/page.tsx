import Link from "next/link";
import { requireActiveUser } from "@/lib/supabase/auth";
import { DeviceCard } from "@/components/dashboard/DeviceCard";
import { FirmwareBanner } from "@/components/dashboard/FirmwareBanner";
import type { Device, FirmwareVersion } from "@/lib/supabase/types";

export default async function DashboardPage() {
  const { user, supabase } = await requireActiveUser();

  const { data: devices } = await supabase
    .from("devices")
    .select("*")
    .eq("owner_id", user.id)
    .eq("status", "claimed")
    .order("created_at", { ascending: false })
    .returns<Device[]>();

  const list = devices ?? [];

  const { data: pendingStatuses } = list.length
    ? await supabase
        .from("device_firmware_status")
        .select("device_id, firmware_version_id, status")
        .in(
          "device_id",
          list.map((d) => d.id)
        )
        .eq("status", "notified")
    : { data: [] };

  let firmwareById: Record<string, FirmwareVersion> = {};
  if (pendingStatuses?.length) {
    const { data: versions } = await supabase
      .from("firmware_versions")
      .select("*")
      .in(
        "id",
        pendingStatuses.map((s) => s.firmware_version_id)
      )
      .returns<FirmwareVersion[]>();
    firmwareById = Object.fromEntries((versions ?? []).map((v) => [v.id, v]));
  }

  return (
    <div>
      <div className="mb-6 flex items-end justify-between">
        <div>
          <h1 className="mb-1 text-2xl font-bold tracking-tight">
            Mis máquinas
          </h1>
          <p className="text-[13px] text-ink-muted">
            {list.length} máquina{list.length === 1 ? "" : "s"} asignada
            {list.length === 1 ? "" : "s"}
          </p>
        </div>
        <Link
          href="/dashboard/agregar"
          className="rounded-lg bg-brand px-[18px] py-2.5 text-[13px] font-semibold text-white transition hover:opacity-90"
        >
          + Agregar máquina
        </Link>
      </div>

      {pendingStatuses?.map((s) => {
        const device = list.find((d) => d.id === s.device_id);
        const firmware = firmwareById[s.firmware_version_id];
        if (!device || !firmware) return null;
        return (
          <FirmwareBanner
            key={`${s.device_id}-${s.firmware_version_id}`}
            deviceId={device.id}
            deviceLabel={device.alias ?? device.mac}
            version={firmware.version}
            changelog={firmware.changelog}
            firmwareVersionId={firmware.id}
          />
        );
      })}

      {list.length === 0 ? (
        <div className="rounded-2xl border border-dashed border-line-soft p-12 text-center">
          <p className="mb-4 text-sm text-ink-muted">
            Todavía no tenés ninguna máquina vinculada.
          </p>
          <Link
            href="/dashboard/agregar"
            className="inline-block rounded-lg bg-brand px-5 py-2.5 text-sm font-semibold text-white"
          >
            + Agregar máquina
          </Link>
        </div>
      ) : (
        <div className="grid grid-cols-2 gap-5">
          {list.map((d) => (
            <DeviceCard key={d.id} device={d} />
          ))}
        </div>
      )}
    </div>
  );
}
