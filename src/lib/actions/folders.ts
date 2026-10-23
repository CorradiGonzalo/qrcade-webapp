"use server";

import { revalidatePath } from "next/cache";
import { createClient } from "@/lib/supabase/server";

async function sessionSupabase() {
  const supabase = await createClient();
  const {
    data: { user },
  } = await supabase.auth.getUser();
  if (!user) throw new Error("No autenticado.");

  const { data: profile } = await supabase
    .from("profiles")
    .select("role")
    .eq("id", user.id)
    .single();

  return { supabase, userId: user.id, isAdmin: profile?.role === "admin" };
}

// Recorre la cadena de padres de `startId` hacia arriba y devuelve true si
// en algún punto llega a `targetId` — o sea, si mover `targetId` para que
// su padre pase a ser `startId` crearía un ciclo (una carpeta terminando
// siendo ancestro de sí misma). Como el árbol de cada dueño es chico en la
// práctica, un recorrido simple alcanza — no hace falta una CTE recursiva
// en SQL para esto.
async function creariaCiclo(
  supabase: Awaited<ReturnType<typeof createClient>>,
  startId: string,
  targetId: string
): Promise<boolean> {
  let cursor: string | null = startId;
  let vueltas = 0;
  while (cursor && vueltas < 100) {
    if (cursor === targetId) return true;
    const res = await supabase
      .from("device_folders")
      .select("parent_id")
      .eq("id", cursor)
      .maybeSingle();
    const data = res.data as { parent_id: string | null } | null;
    cursor = data?.parent_id ?? null;
    vueltas++;
  }
  return false;
}

export type CreateFolderState = { error?: string; ok?: boolean; id?: string } | undefined;

export async function createFolderAction(
  _prevState: CreateFolderState,
  formData: FormData
): Promise<CreateFolderState> {
  const { supabase, userId } = await sessionSupabase();

  const name = String(formData.get("name") || "").trim();
  const parentIdRaw = String(formData.get("parent_id") || "").trim();
  const parentId = parentIdRaw.length > 0 ? parentIdRaw : null;

  if (!name) return { error: "Ponele un nombre a la carpeta." };
  if (name.length > 60) return { error: "El nombre es demasiado largo." };

  if (parentId) {
    const { data: parent } = await supabase
      .from("device_folders")
      .select("id, owner_id")
      .eq("id", parentId)
      .maybeSingle();
    if (!parent || parent.owner_id !== userId) {
      return { error: "La carpeta madre no existe o no es tuya." };
    }
  }

  const { data, error } = await supabase
    .from("device_folders")
    .insert({ owner_id: userId, parent_id: parentId, name })
    .select("id")
    .single();

  if (error) return { error: "No se pudo crear la carpeta." };

  revalidatePath("/dashboard");
  revalidatePath("/admin/equipos");
  return { ok: true, id: data.id };
}

export async function renameFolderAction(folderId: string, name: string) {
  const { supabase, userId } = await sessionSupabase();

  const clean = name.trim();
  if (!clean) throw new Error("El nombre no puede quedar vacío.");
  if (clean.length > 60) throw new Error("El nombre es demasiado largo.");

  const { error } = await supabase
    .from("device_folders")
    .update({ name: clean })
    .eq("id", folderId)
    .eq("owner_id", userId);

  if (error) throw new Error("No se pudo renombrar la carpeta.");

  revalidatePath("/dashboard");
  revalidatePath("/admin/equipos");
}

/**
 * Borra una carpeta. Las subcarpetas se borran en cascada (ON DELETE
 * CASCADE en la FK) y las máquinas que estaban adentro quedan "sueltas"
 * (folder_id -> null, ON DELETE SET NULL) — nunca se borra ni se
 * desvincula una máquina por borrar una carpeta.
 */
export async function deleteFolderAction(folderId: string) {
  const { supabase, userId } = await sessionSupabase();

  const { error } = await supabase
    .from("device_folders")
    .delete()
    .eq("id", folderId)
    .eq("owner_id", userId);

  if (error) throw new Error("No se pudo borrar la carpeta.");

  revalidatePath("/dashboard");
  revalidatePath("/admin/equipos");
}

export async function moveFolderAction(folderId: string, newParentId: string | null) {
  const { supabase, userId } = await sessionSupabase();

  if (folderId === newParentId) {
    throw new Error("Una carpeta no puede ser madre de sí misma.");
  }

  if (newParentId) {
    const { data: parent } = await supabase
      .from("device_folders")
      .select("id, owner_id")
      .eq("id", newParentId)
      .maybeSingle();
    if (!parent || parent.owner_id !== userId) {
      throw new Error("La carpeta destino no existe o no es tuya.");
    }
    if (await creariaCiclo(supabase, newParentId, folderId)) {
      throw new Error("No podés mover una carpeta adentro de una de sus propias subcarpetas.");
    }
  }

  const { error } = await supabase
    .from("device_folders")
    .update({ parent_id: newParentId })
    .eq("id", folderId)
    .eq("owner_id", userId);

  if (error) throw new Error("No se pudo mover la carpeta.");

  revalidatePath("/dashboard");
  revalidatePath("/admin/equipos");
}

/**
 * Asigna (o saca, con folderId = null) una máquina de una carpeta. Vale
 * tanto para un dueño normal (sólo sobre sus propias máquinas, a una
 * carpeta de su propio árbol) como para el admin (cualquier máquina, a
 * una carpeta de SU árbol de admin) — en ambos casos la RLS de `devices`
 * y `device_folders` ya resuelve el permiso real; acá sólo validamos que
 * la carpeta elegida sea del mismo árbol que está operando quien llama.
 */
export async function moveDeviceToFolderAction(
  deviceId: string,
  folderId: string | null
) {
  const { supabase, userId, isAdmin } = await sessionSupabase();

  if (folderId) {
    const { data: folder } = await supabase
      .from("device_folders")
      .select("id, owner_id")
      .eq("id", folderId)
      .maybeSingle();
    if (!folder) throw new Error("La carpeta no existe.");
    if (!isAdmin && folder.owner_id !== userId) {
      throw new Error("Esa carpeta no es tuya.");
    }
  }

  const { error } = await supabase
    .from("devices")
    .update({ folder_id: folderId })
    .eq("id", deviceId);

  if (error) throw new Error("No se pudo mover la máquina.");

  revalidatePath("/dashboard");
  revalidatePath("/admin/equipos");
}
