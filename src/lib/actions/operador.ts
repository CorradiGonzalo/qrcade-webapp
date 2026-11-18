"use server";

import { revalidatePath } from "next/cache";
import { createClient } from "@/lib/supabase/server";
import { createAdminClient } from "@/lib/supabase/admin";
import { logDeviceEvent } from "@/lib/device-events";
import type { Profile } from "@/lib/supabase/types";

/**
 * "Ficha gratis" — lo que ve y toca un operador (ver migración 0020).
 * Dispara EXACTAMENTE el mismo comando que el botón "Test" del dueño
 * (device_commands.command = 'test_dispense', el que ya ejecuta la
 * placa sin tocar ningún firmware nuevo) en vez del comando de
 * limpieza: ese es configurable por el dueño (duración, intervalo) y
 * además prende is_cleaning, que oculta el QR en la pantalla externa
 * -- efectos colaterales que no tienen nada que ver con esto.
 *
 * No recibe deviceId por parámetro a propósito: la máquina a la que
 * puede darle una ficha este operador es la que ya tiene atada en su
 * propio profile (operator_device_id), nunca una que el cliente le
 * pase -- así no hay manera de que un operador le mande un comando a
 * una máquina que no es la suya, pase lo que pase en el formulario.
 *
 * Usa el cliente ADMIN para el insert en device_commands (la policy de
 * insert exige owner_id = auth.uid(), y acá quien actúa es el
 * operador, no el dueño) -- la autorización real es el chequeo de
 * operator_device_id/operator_active de acá abajo, no la RLS.
 */
export async function giveFreeFichaAction(): Promise<{ deviceAlias: string | null }> {
  const supabase = await createClient();
  const {
    data: { user },
  } = await supabase.auth.getUser();
  if (!user) throw new Error("No autenticado.");

  const { data: profile } = await supabase
    .from("profiles")
    .select("role, operator_device_id, operator_active, display_name, username")
    .eq("id", user.id)
    .single<
      Pick<
        Profile,
        "role" | "operator_device_id" | "operator_active" | "display_name" | "username"
      >
    >();

  if (!profile || profile.role !== "operador" || !profile.operator_device_id) {
    throw new Error("Esta cuenta no tiene acceso a ninguna máquina.");
  }
  if (!profile.operator_active) {
    throw new Error("Tu acceso fue desactivado. Hablá con el dueño de la máquina.");
  }

  const admin = createAdminClient();
  const deviceId = profile.operator_device_id;

  const { data: device } = await admin
    .from("devices")
    .select("id, alias, mac, status")
    .eq("id", deviceId)
    .maybeSingle();

  if (!device || device.status === "revoked") {
    throw new Error("La máquina asociada a tu cuenta ya no está disponible.");
  }

  const { error: commandError } = await admin.from("device_commands").insert({
    device_id: deviceId,
    command: "test_dispense",
    created_by: user.id,
  });
  if (commandError) throw new Error(commandError.message);

  const nombre = profile.display_name || profile.username;
  await logDeviceEvent(
    admin,
    deviceId,
    "ficha_gratis",
    `${nombre} dio una ficha gratis.`
  );

  revalidatePath("/operador");
  return { deviceAlias: device.alias ?? device.mac };
}
