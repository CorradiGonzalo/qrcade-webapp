"use server";

import { revalidatePath } from "next/cache";
import { createClient } from "@/lib/supabase/server";
import { createAdminClient } from "@/lib/supabase/admin";
import { generateTemporaryPassword, generateClaimCode } from "@/lib/device-codes";

async function requireAdminSession() {
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

  if (profile?.role !== "admin") throw new Error("No autorizado.");
  return user;
}

export type CreateUserState =
  | { error?: string; ok?: true; username?: string; tempPassword?: string }
  | undefined;

export async function createUserAction(
  _prevState: CreateUserState,
  formData: FormData
): Promise<CreateUserState> {
  await requireAdminSession();

  const username = String(formData.get("username") || "")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9._-]/g, "");
  const displayName = String(formData.get("display_name") || "").trim();

  if (!username || username.length < 3) {
    return { error: "El usuario tiene que tener al menos 3 caracteres." };
  }

  const admin = createAdminClient();
  const tempPassword = generateTemporaryPassword();
  const internalEmail = `${username}@qrcade.internal`;

  const { data: created, error: createError } =
    await admin.auth.admin.createUser({
      email: internalEmail,
      password: tempPassword,
      email_confirm: true,
    });

  if (createError || !created.user) {
    return {
      error:
        createError?.message === "User already registered"
          ? "Ya existe un usuario con ese nombre."
          : "No se pudo crear el usuario.",
    };
  }

  const { error: profileError } = await admin.from("profiles").insert({
    id: created.user.id,
    username,
    display_name: displayName || null,
    role: "usuario",
    must_change_password: true,
  });

  if (profileError) {
    // Rollback del auth.user para no dejar un usuario huérfano
    await admin.auth.admin.deleteUser(created.user.id);
    return { error: "No se pudo crear el usuario." };
  }

  revalidatePath("/admin/usuarios");
  return { ok: true, username, tempPassword };
}

export async function revokeDeviceAction(deviceId: string) {
  await requireAdminSession();
  const admin = createAdminClient();
  const { error } = await admin
    .from("devices")
    .update({ status: "revoked", revoked_at: new Date().toISOString() })
    .eq("id", deviceId);
  if (error) throw new Error(error.message);
  revalidatePath("/admin/equipos");
}

export async function reactivateDeviceAction(deviceId: string) {
  await requireAdminSession();
  const admin = createAdminClient();
  const { data: device } = await admin
    .from("devices")
    .select("owner_id")
    .eq("id", deviceId)
    .single();

  const { error } = await admin
    .from("devices")
    .update({
      status: device?.owner_id ? "claimed" : "unclaimed",
      revoked_at: null,
    })
    .eq("id", deviceId);
  if (error) throw new Error(error.message);
  revalidatePath("/admin/equipos");
}

/**
 * Genera un código nuevo para una placa. Sirve tanto para "perdí el
 * código y no llegué a dárselo a nadie" (placa sin reclamar) como para
 * el caso de reventa: alguien le vende la máquina a otra persona, y esa
 * persona la tiene que dar de alta a su nombre. En ese segundo caso hay
 * que soltar al dueño anterior y borrar su configuración de cobro —
 * si no, la placa seguiría cobrando a la cuenta de MP de otro.
 */
export async function regenerateDeviceCodeAction(deviceId: string) {
  await requireAdminSession();
  const admin = createAdminClient();

  // Se suelta la configuración de cobro entera: la placa vuelve a
  // "cobro sin configurar" hasta que el nuevo dueño la configure con SU
  // propia cuenta de MP. Los combos viejos también se borran (quedarían
  // huérfanos si no).
  await admin.from("device_ficha_combos").delete().eq("device_id", deviceId);

  const { error } = await admin
    .from("devices")
    .update({
      status: "unclaimed",
      claim_code: generateClaimCode(),
      owner_id: null,
      alias: null,
      claimed_at: null,
      is_paused: false,
      mode: "fijo",
      local_name: null,
      caja_name: null,
      mp_monto_fijo: null,
      store_id: null,
      pos_id: null,
      qr_data: null,
      provisioned_at: null,
    })
    .eq("id", deviceId);

  if (error) throw new Error(error.message);
  revalidatePath("/admin/equipos");
}

export type UploadFirmwareState = { error?: string; ok?: boolean } | undefined;

export async function uploadFirmwareAction(
  _prevState: UploadFirmwareState,
  formData: FormData
): Promise<UploadFirmwareState> {
  const user = await requireAdminSession();

  const version = String(formData.get("version") || "").trim();
  const binUrl = String(formData.get("bin_url") || "").trim();
  const changelog = String(formData.get("changelog") || "").trim();

  if (!version || !binUrl) {
    return { error: "Faltan la versión o el archivo .bin." };
  }

  const admin = createAdminClient();

  const { data: firmware, error } = await admin
    .from("firmware_versions")
    .insert({
      version,
      bin_url: binUrl,
      changelog: changelog || null,
      created_by: user.id,
    })
    .select("id")
    .single();

  if (error) {
    return {
      error:
        error.code === "23505"
          ? "Ya existe una versión con ese nombre."
          : "No se pudo registrar la versión.",
    };
  }

  // Notificamos a todas las máquinas reclamadas — cada dueño decide si
  // instala o no desde su panel.
  const { data: devices } = await admin
    .from("devices")
    .select("id")
    .eq("status", "claimed");

  if (devices?.length) {
    await admin.from("device_firmware_status").insert(
      devices.map((d) => ({
        device_id: d.id,
        firmware_version_id: firmware.id,
        status: "notified" as const,
      }))
    );
  }

  revalidatePath("/admin/firmware");
  return { ok: true };
}
