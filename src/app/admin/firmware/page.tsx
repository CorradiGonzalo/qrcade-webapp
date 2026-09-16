import { requireAdmin } from "@/lib/supabase/auth";
import { UploadFirmwareForm } from "@/components/admin/UploadFirmwareForm";
import { Card } from "@/components/Card";
import { Badge } from "@/components/Badge";
import type { Device, FirmwareVersion, Profile } from "@/lib/supabase/types";

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

  const latest = versions?.[0];

  let statusByDevice: Record<string, string> = {};
  if (latest) {
    const { data: statuses } = await supabase
      .from("device_firmware_status")
      .select("device_id, status")
      .eq("firmware_version_id", latest.id);
    statusByDevice = Object.fromEntries(
      (statuses ?? []).map((s) => [s.device_id, s.status])
    );
  }

  const STATUS_BADGE: Record<string, { tone: "ok" | "warn" | "info" | "muted"; label: string }> = {
    updated: { tone: "ok", label: "✓ Actualizada" },
    accepted: { tone: "info", label: "↻ Descargando" },
    downloading: { tone: "info", label: "↻ Descargando" },
    notified: { tone: "warn", label: "● Notificada" },
    dismissed: { tone: "muted", label: "— Postergada" },
    failed: { tone: "warn", label: "⚠ Falló" },
  };

  return (
    <div>
      <div className="mb-6 flex items-start justify-between">
        <div>
          <h1 className="text-2xl font-bold tracking-tight">Firmware</h1>
          <p className="mt-1 max-w-[560px] text-[13px] text-ink-muted">
            Subís el .bin y queda disponible — cada dueño decide cuándo
            instalarlo en su máquina. Vos no forzás nada.
          </p>
        </div>
      </div>

      <div className="grid grid-cols-[1fr_1.4fr] gap-6">
        <UploadFirmwareForm />

        <div>
          <div className="mb-3 text-sm font-bold">Versiones publicadas</div>
          <div className="space-y-3">
            {(versions ?? []).length === 0 && (
              <p className="text-xs text-ink-faint">
                Todavía no subiste ninguna versión.
              </p>
            )}
            {(versions ?? []).map((v, i) => (
              <Card key={v.id} highlight={i === 0}>
                <div className="mb-1.5 flex items-center justify-between">
                  <div className="text-[15px] font-bold">
                    {v.version}{" "}
                    <span className="text-xs font-normal text-info">
                      · {v.bin_url.split("/").pop()}
                    </span>
                  </div>
                  <Badge tone={i === 0 ? "info" : "muted"}>
                    {i === 0 ? "ACTUAL" : "ANTERIOR"}
                  </Badge>
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
            ))}
          </div>

          {latest && (devices ?? []).length > 0 && (
            <>
              <div className="mb-2 mt-6 grid grid-cols-[2fr_1.4fr_1fr] px-1 text-[11px] font-bold tracking-wide text-ink-faint">
                <div>MÁQUINA</div>
                <div>DUEÑO</div>
                <div>ESTADO DE {latest.version}</div>
              </div>
              <div className="space-y-2">
                {(devices ?? []).map((d) => {
                  const status = statusByDevice[d.id] ?? "notified";
                  const badge = STATUS_BADGE[status] ?? STATUS_BADGE.notified;
                  return (
                    <div
                      key={d.id}
                      className="grid grid-cols-[2fr_1.4fr_1fr] items-center rounded-[10px] border border-line bg-surface p-4"
                    >
                      <div className="text-[13px] font-semibold">
                        {d.alias ?? d.mac}
                      </div>
                      <div className="text-xs text-ink-muted">
                        {ownerName(d.owner_id)}
                      </div>
                      <div>
                        <Badge tone={badge.tone}>{badge.label}</Badge>
                      </div>
                    </div>
                  );
                })}
              </div>
            </>
          )}
        </div>
      </div>
    </div>
  );
}
