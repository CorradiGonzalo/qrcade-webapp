import { requireAdmin } from "@/lib/supabase/auth";
import { UploadFirmwareForm } from "@/components/admin/UploadFirmwareForm";
import { CancelUpdateButton } from "@/components/admin/CancelUpdateButton";
import { Card } from "@/components/Card";
import { Badge } from "@/components/Badge";
import type { Device, DeviceKind, FirmwareVersion, Profile } from "@/lib/supabase/types";

const KIND_LABEL: Record<DeviceKind, string> = {
  garra: "🕹 Garra",
  cafetera: "☕ Cafetera",
};

const STATUS_BADGE: Record<string, { tone: "ok" | "warn" | "info" | "muted"; label: string }> = {
  updated: { tone: "ok", label: "✓ Actualizada" },
  accepted: { tone: "info", label: "↻ Descargando" },
  downloading: { tone: "info", label: "↻ Descargando" },
  notified: { tone: "warn", label: "● Notificada" },
  dismissed: { tone: "muted", label: "— Postergada" },
  failed: { tone: "warn", label: "⚠ Falló" },
};

export default async function AdminFirmwarePage() {
  const { supabase } = await requireAdmin();

  const { data: versions } = await supabase
    .from("firmware_versions")
    .select("*")
    .order("created_at", { ascending: false })
    .returns<FirmwareVersion[]>();

  const { data: devices } = await supabase
    .from("devices")
    .select("*")
    .eq("status", "claimed")
    .order("created_at", { ascending: false })
    .returns<Device[]>();

  const ownerIds = [
    ...new Set((devices ?? []).map((d) => d.owner_id).filter(Boolean)),
  ] as string[];

  const { data: owners } = ownerIds.length
    ? await supabase
        .from("profiles")
        .select("id, username, display_name")
        .in("id", ownerIds)
        .returns<Pick<Profile, "id" | "username" | "display_name">[]>()
    : { data: [] as Pick<Profile, "id" | "username" | "display_name">[] };

  const ownerName = (id: string | null) => {
    const o = owners?.find((x) => x.id === id);
    return o ? o.display_name ?? o.username : "—";
  };

  // La "versión actual" ya no es una sola: garra y cafetera tienen cada
  // una su propia última versión, y jamás se mezclan.
  const latestByKind: Partial<Record<DeviceKind, FirmwareVersion>> = {};
  for (const v of versions ?? []) {
    if (!latestByKind[v.kind]) latestByKind[v.kind] = v;
  }

  const latestIds = Object.values(latestByKind).map((v) => v!.id);
  let statusByDeviceAndVersion: Record<string, string> = {};
  if (latestIds.length) {
    const { data: statuses } = await supabase
      .from("device_firmware_status")
      .select("device_id, firmware_version_id, status")
      .in("firmware_version_id", latestIds);
    statusByDeviceAndVersion = Object.fromEntries(
      (statuses ?? []).map((s) => [`${s.device_id}:${s.firmware_version_id}`, s.status])
    );
  }

  return (
    <div>
      <div className="mb-6 flex items-start justify-between">
        <div>
          <h1 className="text-2xl font-bold tracking-tight">Firmware</h1>
          <p className="mt-1 max-w-[560px] text-[13px] text-ink-muted">
            Subís el .bin y queda disponible — cada dueño decide cuándo
            instalarlo en su máquina. Vos no forzás nada. Garra y cafetera
            tienen versiones separadas: nunca se ofrece el .bin de una a la
            otra.
          </p>
        </div>
      </div>

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-[1fr_1.4fr]">
        <UploadFirmwareForm />

        <div>
          <div className="mb-3 text-sm font-bold">Versiones publicadas</div>
          <div className="space-y-3">
            {(versions ?? []).length === 0 && (
              <p className="text-xs text-ink-faint">
                Todavía no subiste ninguna versión.
              </p>
            )}
            {(versions ?? []).map((v) => {
              const esActual = latestByKind[v.kind]?.id === v.id;
              return (
                <Card key={v.id} highlight={esActual}>
                  <div className="mb-1.5 flex items-center justify-between">
                    <div className="text-[15px] font-bold">
                      {v.version}{" "}
                      <span className="text-xs font-normal text-info">
                        · {v.bin_url.split("/").pop()}
                      </span>
                    </div>
                    <div className="flex items-center gap-1.5">
                      <Badge tone="muted">{KIND_LABEL[v.kind]}</Badge>
                      <Badge tone={esActual ? "info" : "muted"}>
                        {esActual ? "ACTUAL" : "ANTERIOR"}
                      </Badge>
                    </div>
                  </div>
                  {v.changelog && (
                    <p className="mb-2 text-xs leading-relaxed text-ink-muted">
                      {v.changelog}
                    </p>
                  )}
                  <div className="text-[11px] text-ink-faint">
                    Subida {new Date(v.created_at).toLocaleDateString("es-AR")}
                  </div>
                </Card>
              );
            })}
          </div>

          {(Object.keys(latestByKind) as DeviceKind[]).map((kind) => {
            const latest = latestByKind[kind]!;
            const devicesDeEsteTipo = (devices ?? []).filter((d) => d.kind === kind);
            if (devicesDeEsteTipo.length === 0) return null;
            return (
              <div key={kind}>
                <div className="mb-2 mt-6 text-xs font-bold text-ink-muted">
                  {KIND_LABEL[kind]} — estado de {latest.version}
                </div>
                <div className="mb-2 hidden grid-cols-[2fr_1.4fr_1fr] px-1 text-[11px] font-bold tracking-wide text-ink-faint sm:grid">
                  <div>MÁQUINA</div>
                  <div>DUEÑO</div>
                  <div>ESTADO</div>
                </div>
                <div className="space-y-2">
                  {devicesDeEsteTipo.map((d) => {
                    const status =
                      statusByDeviceAndVersion[`${d.id}:${latest.id}`] ?? "—";
                    const badge =
                      STATUS_BADGE[status] ?? { tone: "muted" as const, label: "—" };
                    return (
                      <div
                        key={d.id}
                        className="grid grid-cols-1 gap-1.5 rounded-[10px] border border-line bg-surface p-4 sm:grid-cols-[2fr_1.4fr_1fr] sm:items-center sm:gap-0"
                      >
                        <div className="text-[13px] font-semibold">
                          {d.alias ?? d.mac}
                        </div>
                        <div className="text-xs text-ink-muted">
                          {ownerName(d.owner_id)}
                        </div>
                        <div className="flex items-center">
                          <Badge tone={badge.tone}>{badge.label}</Badge>
                          {(status === "accepted" || status === "downloading") && (
                            <CancelUpdateButton
                              deviceId={d.id}
                              firmwareVersionId={latest.id}
                            />
                          )}
                        </div>
                      </div>
                    );
                  })}
                </div>
              </div>
            );
          })}
        </div>
      </div>
    </div>
  );
}
