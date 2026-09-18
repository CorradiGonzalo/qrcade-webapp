"use server";

import { revalidatePath } from "next/cache";
import { createClient } from "@/lib/supabase/server";
import { createAdminClient } from "@/lib/supabase/admin";
import { logDeviceEvent } from "@/lib/device-events";
import type { Device, Profile } from "@/lib/supabase/types";
import {
  MercadoPagoError,
  abrirMontoQR,
  crearOrdenMontoFijo,
  buscarOCrearCaja,
  buscarOCrearTienda,
  externalIdBase,
  obtenerUserId,
  sanitizar,
  soloAlfanumerico,
} from "@/lib/mercadopago";

async function ownedSupabase() {
  const supabase = await createClient();
  const {
    data: { user },
  } = await supabase.auth.getUser();
  if (!user) throw new Error("No autenticado.");
  return { supabase, userId: user.id };
}

// =====================================================================
//                    TOKEN DE CUENTA (a nivel de dueño)
// =====================================================================
export type SaveAccountTokenState = { error?: string; ok?: boolean } | undefined;

export async function saveAccountTokenAction(
  _prevState: SaveAccountTokenState,
  formData: FormData
): Promise<SaveAccountTokenState> {
  const { supabase, userId } = await ownedSupabase();

  const token = String(formData.get("mp_access_token") || "").trim();

  if (!token) {
    const { error } = await supabase
      .from("profiles")
      .update({ mp_access_token: null, mp_user_id: null })
      .eq("id", userId);
    if (error) return { error: "No se pudo guardar." };
    revalidatePath("/dashboard/cuenta");
    return { ok: true };
  }

  let mpUserId: string;
  try {
    mpUserId = await obtenerUserId(token);
  } catch (err) {
    const msg =
      err instanceof MercadoPagoError
        ? err.message
        : "No se pudo validar el token.";
    return { error: `Token inválido: ${msg}` };
  }

  const { error } = await supabase
    .from("profiles")
    .update({ mp_access_token: token, mp_user_id: mpUserId })
    .eq("id", userId);

  if (error) return { error: "No se pudo guardar. Probá de nuevo." };

  revalidatePath("/dashboard/cuenta");
  return { ok: true };
}

// =====================================================================
//              CONFIGURACIÓN DE COBRO POR PLACA (local/caja/modo)
// =====================================================================
export type SaveDeviceBillingState = { error?: string; ok?: boolean } | undefined;

export async function saveDeviceBillingAction(
  _prevState: SaveDeviceBillingState,
  formData: FormData
): Promise<SaveDeviceBillingState> {
  const { supabase, userId } = await ownedSupabase();

  const deviceId = String(formData.get("device_id") || "");
  const mode = String(formData.get("mode") || "fijo") as "fijo" | "combo";
  const localName = String(formData.get("local_name") || "").trim();
  const cajaName = String(formData.get("caja_name") || "").trim();
  const montoRaw = String(formData.get("mp_monto_fijo") || "").trim();
  const monto = montoRaw ? Number(montoRaw) : null;

  if (!deviceId) return { error: "Falta la máquina." };
  if (!localName || !cajaName) {
    return { error: "Completá el nombre del local y de la caja." };
  }
  if (mode === "fijo" && (monto === null || Number.isNaN(monto) || monto <= 0)) {
    return { error: "En modo monto fijo, el monto tiene que ser mayor a 0." };
  }
  if (!["fijo", "combo"].includes(mode)) {
    return { error: "Modo inválido." };
  }

  const { data: profile } = await supabase
    .from("profiles")
    .select("mp_access_token, mp_user_id")
    .eq("id", userId)
    .single<Pick<Profile, "mp_access_token" | "mp_user_id">>();

  if (!profile?.mp_access_token || !profile.mp_user_id) {
    return {
      error:
        "Primero cargá el Access Token de tu cuenta de Mercado Pago (Mi cuenta).",
    };
  }

  const { data: device } = await supabase
    .from("devices")
    .select("*")
    .eq("id", deviceId)
    .eq("owner_id", userId)
    .single<Device>();

  if (!device) return { error: "No se encontró la máquina." };

  const token = profile.mp_access_token;
  const mpUserId = profile.mp_user_id;
  const base = externalIdBase(device.id);
  const externalIdTienda = "TIENDA-" + sanitizar(localName) + "-" + base;
  const externalIdCaja = soloAlfanumerico("CAJA" + sanitizar(cajaName) + base);

  let qrData: string;
  try {
    const storeId = await buscarOCrearTienda(token, mpUserId, externalIdTienda, localName);
    qrData = await buscarOCrearCaja(token, storeId, externalIdCaja, cajaName);

    if (mode === "fijo" && monto !== null) {
      await crearOrdenMontoFijo(token, externalIdCaja, cajaName, monto);
    } else {
      await abrirMontoQR(token, mpUserId, externalIdCaja);
    }

    const { error } = await supabase
      .from("devices")
      .update({
        mode,
        local_name: localName,
        caja_name: cajaName,
        mp_monto_fijo: mode === "fijo" ? monto : null,
        store_id: storeId,
        pos_id: externalIdCaja,
        qr_data: qrData,
        provisioned_at: new Date().toISOString(),
      })
      .eq("id", deviceId);

    if (error) return { error: "Se aprovisionó en Mercado Pago pero no se pudo guardar. Probá de nuevo." };
  } catch (err) {
    const msg =
      err instanceof MercadoPagoError
        ? err.message
        : "Error inesperado hablando con Mercado Pago.";
    return { error: msg };
  }

  revalidatePath(`/dashboard/maquinas/${deviceId}`);
  revalidatePath(`/dashboard/maquinas/${deviceId}/cobro`);
  return { ok: true };
}

// =====================================================================
//                    COMBOS DE FICHAS (modo "combo")
// =====================================================================
export async function addComboAction(deviceId: string, fichas: number, monto: number) {
  const { supabase } = await ownedSupabase();

  if (!Number.isInteger(fichas) || fichas <= 0) {
    throw new Error("La cantidad de fichas tiene que ser un entero mayor a 0.");
  }
  if (!(monto > 0)) {
    throw new Error("El monto tiene que ser mayor a 0.");
  }

  const { error } = await supabase
    .from("device_ficha_combos")
    .insert({ device_id: deviceId, fichas, monto });

  if (error) {
    throw new Error(
      error.code === "23505"
        ? "Ya hay un combo con ese monto exacto."
        : "No se pudo guardar el combo."
    );
  }

  revalidatePath(`/dashboard/maquinas/${deviceId}/cobro`);
}

export async function removeComboAction(comboId: string, deviceId: string) {
  const { supabase } = await ownedSupabase();
  const { error } = await supabase
    .from("device_ficha_combos")
    .delete()
    .eq("id", comboId);
  if (error) throw new Error(error.message);
  revalidatePath(`/dashboard/maquinas/${deviceId}/cobro`);
}

export async function togglePauseAction(deviceId: string, pause: boolean) {
  const { supabase } = await ownedSupabase();
  const { error } = await supabase
    .from("devices")
    .update({ is_paused: pause })
    .eq("id", deviceId);
  if (error) throw new Error(error.message);
  revalidatePath(`/dashboard/maquinas/${deviceId}`);
  revalidatePath("/dashboard");
}

export async function requestRestartAction(deviceId: string) {
  const { supabase, userId } = await ownedSupabase();
  const { error } = await supabase.from("device_commands").insert({
    device_id: deviceId,
    command: "restart",
    created_by: userId,
  });
  if (error) throw new Error(error.message);

  await logDeviceEvent(
    createAdminClient(),
    deviceId,
    "restart_requested",
    "Reinicio solicitado desde el panel."
  );

  revalidatePath(`/dashboard/maquinas/${deviceId}`);
}

/**
 * Dispara el relé una vez, a modo de prueba — reemplaza al viejo comando
 * 'c' que se mandaba por serial desde el Arduino IDE. No suma ninguna
 * venta ni descuenta fichas, sólo confirma que el mecanismo anda.
 */
export async function requestTestDispenseAction(deviceId: string) {
  const { supabase, userId } = await ownedSupabase();
  const { error } = await supabase.from("device_commands").insert({
    device_id: deviceId,
    command: "test_dispense",
    created_by: userId,
  });
  if (error) throw new Error(error.message);

  // device_events sólo lo inserta el backend con el cliente admin (no hay
  // policy de insert para el usuario logueado).
  await logDeviceEvent(
    createAdminClient(),
    deviceId,
    "dispense_test",
    "Tirando ficha (prueba manual desde el panel)."
  );

  revalidatePath(`/dashboard/maquinas/${deviceId}`);
}

export async function respondFirmwareAction(
  deviceId: string,
  firmwareVersionId: string,
  status: "accepted" | "dismissed"
) {
  const { supabase } = await ownedSupabase();
  const { error } = await supabase
    .from("device_firmware_status")
    .update({ status, updated_at: new Date().toISOString() })
    .eq("device_id", deviceId)
    .eq("firmware_version_id", firmwareVersionId);
  if (error) throw new Error(error.message);
  revalidatePath("/dashboard");
  revalidatePath(`/dashboard/maquinas/${deviceId}`);
}

export type ClaimDeviceState = { error?: string; ok?: boolean } | undefined;

export async function claimDeviceAction(
  _prevState: ClaimDeviceState,
  formData: FormData
): Promise<ClaimDeviceState> {
  const { userId } = await ownedSupabase();

  const code = String(formData.get("code") || "")
    .trim()
    .toUpperCase();
  const alias = String(formData.get("alias") || "").trim() || null;

  if (code.length < 4) return { error: "Ingresá el código completo." };

  // Una placa "unclaimed" (owner_id null) no es visible por RLS para un
  // usuario común todavía, así que el match por código se hace con el
  // cliente admin — recién después de validar que el código existe y
  // está libre le asignamos el owner_id de quien está logueado.
  const admin = createAdminClient();

  const { data: device, error: findError } = await admin
    .from("devices")
    .select("id, status")
    .eq("claim_code", code)
    .eq("status", "unclaimed")
    .maybeSingle();

  if (findError) return { error: "No se pudo vincular la máquina." };
  if (!device) {
    return {
      error: "Código inválido o la máquina ya fue vinculada.",
    };
  }

  const { error: updateError } = await admin
    .from("devices")
    .update({
      owner_id: userId,
      status: "claimed",
      claimed_at: new Date().toISOString(),
      claim_code: null,
      alias,
    })
    .eq("id", device.id);

  if (updateError) return { error: "No se pudo vincular la máquina." };

  revalidatePath("/dashboard");
  return { ok: true };
}
