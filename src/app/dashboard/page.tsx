import Link from "next/link";
import { requireActiveUser } from "@/lib/supabase/auth";
import { DeviceCard } from "@/components/dashboard/DeviceCard";
import { FirmwareBanner } from "@/components/dashboard/FirmwareBanner";
import { FolderBrowser } from "@/components/dashboard/FolderBrowser";
import { MoveToFolderMenu } from "@/components/dashboard/MoveToFolderMenu";
import { listFoldersForOwner } from "@/lib/folders";
import type { Device, FirmwareVersion } from "@/lib/supabase/types";

export default async function DashboardPage(
  props: PageProps<"/dashboard">
) {
  const sp = await props.searchParams;
  const { user, supabase } = await requireActiveUser();

  const { data: devices } = await supabase
    .from("devices")
    .select("*")
    .eq("owner_id", user.id)
    .eq("status", "claimed")
    .order("created_at", { ascending: false })
    .returns<Device[]>();

  const all = devices ?? [];
  const folders = await listFoldersForOwner(supabase, user.id);

  const carpetaActual =
    typeof sp.carpeta === "string" && sp.carpeta.length > 0 ? sp.carpeta : null;
  const list = all.filter((d) => (d.folder_id ?? null) === carpetaActual);

  // El aviso de firmware pendiente se chequea sobre TODAS las máquinas
  // del dueño, no sólo las de la carpeta que está mirando ahora — si no,
  // podría no enterarse nunca de que hay una actualización esperando en
  // una máquina que tiene guardada en otra carpeta.
  const { data: pendingStatuses } = all.length
    ? await supabase
        .from("device_firmware_status")
        .select("device_id, firmware_version_id, status")
        .in(
          "device_id",
          all.map((d) => d.id)
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
      <div className="mb-6 flex flex-col gap-3 sm:flex-row sm:items-end sm:justify-between">
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
          className="rounded-lg bg-brand px-[18px] py-2.5 text-center text-[13px] font-semibold text-white transition hover:opacity-90"
        >
          + Agregar máquina
        </Link>
      </div>

      {pendingStatuses?.map((s) => {
        const device = all.find((d) => d.id === s.device_id);
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

      <FolderBrowser folders={folders} deviceCountByFolder={all} />

      {all.length === 0 ? (
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
      ) : list.length === 0 ? (
        <p className="py-10 text-center text-sm text-ink-faint">
          No hay máquinas sueltas acá — entrá a una carpeta o movés alguna
          con el 📁 de su tarjeta.
        </p>
      ) : (
        <div className="grid grid-cols-1 gap-5 sm:grid-cols-2">
          {list.map((d) => (
            <div key={d.id} className="relative">
              <div className="absolute right-3 top-3 z-10">
                <MoveToFolderMenu
                  deviceId={d.id}
                  currentFolderId={d.folder_id}
                  folders={folders}
                />
              </div>
              <DeviceCard device={d} />
            </div>
          ))}
        </div>
      )}
    </div>
  );
}
