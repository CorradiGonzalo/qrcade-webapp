import type { SupabaseClient } from "@supabase/supabase-js";
import type { DeviceFolder } from "@/lib/supabase/types";

/** Lee el árbol de carpetas de un dueño puntual (ownerId = su propio
 * profiles.id, o el del admin para SU árbol aparte). Query simple, no
 * es una Server Action — por eso vive separado de actions/folders.ts. */
export async function listFoldersForOwner(
  supabase: SupabaseClient,
  ownerId: string
): Promise<DeviceFolder[]> {
  const { data } = await supabase
    .from("device_folders")
    .select("*")
    .eq("owner_id", ownerId)
    .order("name", { ascending: true })
    .returns<DeviceFolder[]>();
  return data ?? [];
}

/** Arma hijos-por-padre una sola vez para no recorrer el array entero
 * en cada nivel al renderizar el árbol. */
export function groupFoldersByParent(
  folders: DeviceFolder[]
): Map<string | null, DeviceFolder[]> {
  const byParent = new Map<string | null, DeviceFolder[]>();
  for (const f of folders) {
    const key = f.parent_id;
    if (!byParent.has(key)) byParent.set(key, []);
    byParent.get(key)!.push(f);
  }
  return byParent;
}

/** Camino completo (breadcrumb) desde la raíz hasta `folderId`, inclusive. */
export function folderPath(
  folders: DeviceFolder[],
  folderId: string | null
): DeviceFolder[] {
  const byId = new Map(folders.map((f) => [f.id, f]));
  const path: DeviceFolder[] = [];
  let cursor = folderId ? byId.get(folderId) ?? null : null;
  while (cursor) {
    path.unshift(cursor);
    cursor = cursor.parent_id ? byId.get(cursor.parent_id) ?? null : null;
  }
  return path;
}

/** "Dino Mall / Cafeteras" — para listas planas (ej. el selector de
 * "mover a carpeta") donde no hay lugar para dibujar un árbol real. */
export function folderFullName(folders: DeviceFolder[], folderId: string): string {
  return folderPath(folders, folderId)
    .map((f) => f.name)
    .join(" / ");
}

/** Cuenta cuántas máquinas hay dentro de una carpeta Y de todas sus
 * subcarpetas (recursivo) — para mostrar un total en el tile, no sólo lo
 * que hay en el nivel actual. */
export function countDevicesDeep(
  folders: DeviceFolder[],
  devices: { folder_id: string | null }[],
  folderId: string
): number {
  const byParent = groupFoldersByParent(folders);
  const idsEnRama = new Set<string>([folderId]);
  const pila = [folderId];
  while (pila.length) {
    const actual = pila.pop()!;
    for (const hija of byParent.get(actual) ?? []) {
      if (!idsEnRama.has(hija.id)) {
        idsEnRama.add(hija.id);
        pila.push(hija.id);
      }
    }
  }
  return devices.filter((d) => d.folder_id && idsEnRama.has(d.folder_id)).length;
}
