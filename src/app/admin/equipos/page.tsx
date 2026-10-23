import { requireAdmin } from "@/lib/supabase/auth";
import { Card } from "@/components/Card";
import { DeviceStatusRow } from "@/components/admin/DeviceStatusRow";
import { FolderBrowser } from "@/components/dashboard/FolderBrowser";
import { MoveToFolderMenu } from "@/components/dashboard/MoveToFolderMenu";
import { listFoldersForOwner } from "@/lib/folders";
import type { Device, Profile } from "@/lib/supabase/types";

export default async function AdminEquiposPage(
  props: PageProps<"/admin/equipos">
) {
  const { user, supabase } = await requireAdmin();
  const sp = await props.searchParams;

  const { data: devices } = await supabase
    .from("devices")
    .select("*")
    .order("created_at", { ascending: false })
    .returns<Device[]>();

  // El admin tiene su propio árbol de carpetas (separado del de cada
  // dueño) — puede guardar ahí máquinas de cualquier owner, es sólo
  // organización visual para el panel de admin.
  const folders = await listFoldersForOwner(supabase, user.id);
  const carpetaActual =
    typeof sp.carpeta === "string" && sp.carpeta.length > 0 ? sp.carpeta : null;

  const ownerIds = [...new Set((devices ?? []).map((d) => d.owner_id).filter(Boolean))] as string[];

  const { data: owners } = ownerIds.length
    ? await supabase
        .from("profiles")
        .select("id, username, display_name")
        .in("id", ownerIds)
        .returns<Pick<Profile, "id" | "username" | "display_name">[]>()
    : { data: [] as Pick<Profile, "id" | "username" | "display_name">[] };

  const ownerName = (id: string | null) => {
    if (!id) return null;
    const o = owners?.find((x) => x.id === id);
    return o ? o.display_name ?? o.username : null;
  };

  const all = devices ?? [];
  const list = all.filter((d) => (d.folder_id ?? null) === carpetaActual);
  const counts = {
    claimed: all.filter((d) => d.status === "claimed").length,
    unclaimed: all.filter((d) => d.status === "unclaimed").length,
    revoked: all.filter((d) => d.status === "revoked").length,
  };

  return (
    <div>
      <div className="mb-2">
        <h1 className="text-2xl font-bold tracking-tight">Equipos</h1>
        <p className="mt-1 max-w-[640px] text-[13px] text-ink-muted">
          Cada placa se autoriza sola al primer boot (trae una clave de
          fábrica de tu compilación). Acá solo auditás y podés revocar si
          hace falta — no hay nada que aprobar a mano.
        </p>
      </div>

      <div className="my-6 grid grid-cols-3 gap-2 sm:gap-3.5">
        <Card className="px-3 py-3 sm:px-[18px] sm:py-3.5">
          <div className="text-[11px] font-bold text-ink-faint">
            AUTORIZADAS
          </div>
          <div className="mt-1 text-xl font-bold">{counts.claimed + counts.unclaimed}</div>
        </Card>
        <Card className="px-3 py-3 sm:px-[18px] sm:py-3.5">
          <div className="text-[11px] font-bold text-ink-faint">
            SIN RECLAMAR
          </div>
          <div className="mt-1 text-xl font-bold text-warn">{counts.unclaimed}</div>
        </Card>
        <Card className="px-3 py-3 sm:px-[18px] sm:py-3.5">
          <div className="text-[11px] font-bold text-ink-faint">
            REVOCADAS
          </div>
          <div className="mt-1 text-xl font-bold text-danger">{counts.revoked}</div>
        </Card>
      </div>

      <FolderBrowser folders={folders} deviceCountByFolder={all} />

      <div>
        <div className="hidden grid-cols-[2.2fr_1.6fr_1.2fr_0.5fr_1.4fr] border-b border-line px-4 pb-2.5 text-[11px] font-bold tracking-wide text-ink-faint sm:grid">
          <div>CÓDIGO ID</div>
          <div>DUEÑO</div>
          <div>ALTA</div>
          <div className="text-center">●</div>
          <div />
        </div>

        <div className="mt-2 space-y-2.5">
          {all.length === 0 ? (
            <p className="py-10 text-center text-sm text-ink-faint">
              Todavía no hay ninguna placa que haya hecho check-in.
            </p>
          ) : list.length === 0 ? (
            <p className="py-10 text-center text-sm text-ink-faint">
              No hay máquinas sueltas acá — entrá a una carpeta o movés
              alguna con el 📁 de su fila.
            </p>
          ) : (
            list.map((d) => (
              <div key={d.id} className="flex items-start gap-2">
                <div className="flex-1">
                  <DeviceStatusRow device={d} ownerName={ownerName(d.owner_id)} />
                </div>
                <div className="pt-4">
                  <MoveToFolderMenu
                    deviceId={d.id}
                    currentFolderId={d.folder_id}
                    folders={folders}
                  />
                </div>
              </div>
            ))
          )}
        </div>
      </div>
    </div>
  );
}
