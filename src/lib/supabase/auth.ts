import "server-only";
import { redirect } from "next/navigation";
import { createClient } from "./server";
import { createAdminClient } from "./admin";
import type { Device, Profile } from "./types";

/**
 * Trae el usuario logueado + su fila de profiles. Si no hay sesión,
 * redirige a /login.
 */
export async function requireUser() {
  const supabase = await createClient();
  const {
    data: { user },
  } = await supabase.auth.getUser();

  if (!user) redirect("/login");

  const { data: profile } = await supabase
    .from("profiles")
    .select("*")
    .eq("id", user.id)
    .single<Profile>();

  if (!profile) redirect("/login");

  return { user, profile, supabase };
}

/**
 * Igual que requireUser, pero además fuerza el paso por /primer-ingreso
 * si todavía no cambió la contraseña temporal. Usar en toda página
 * protegida excepto /primer-ingreso.
 */
export async function requireActiveUser() {
  const result = await requireUser();
  if (result.profile.must_change_password) {
    redirect("/primer-ingreso");
  }
  return result;
}

export async function requireAdmin() {
  const result = await requireActiveUser();
  if (result.profile.role !== "admin") redirect("/dashboard");
  return result;
}

/**
 * Para /operador: exige role='operador' (cualquier otro rol se manda a
 * su panel correspondiente, nunca se queda acá). A diferencia de
 * requireAdmin, NO redirige si la cuenta está desactivada
 * (operator_active=false) ni si la máquina ya no existe/fue revocada
 * -- en esos casos no hay a dónde mandarlo, así que la página de
 * /operador es la que le muestra el aviso ("no tenés acceso") en vez
 * de rebotarlo a ningún lado.
 *
 * El dueño de la máquina NO tiene fila en `devices` con owner_id =
 * auth.uid() del operador (es dueño de otra persona), así que esta
 * lectura de `devices` usa el cliente admin a propósito -- la
 * autorización real ya la dio el chequeo de operator_device_id de
 * arriba, no la RLS de devices.
 */
export async function requireOperator() {
  const result = await requireActiveUser();
  const { profile } = result;

  if (profile.role !== "operador") {
    redirect(profile.role === "admin" ? "/admin/equipos" : "/dashboard");
  }

  let device: Device | null = null;
  if (profile.operator_device_id) {
    const admin = createAdminClient();
    const { data } = await admin
      .from("devices")
      .select("*")
      .eq("id", profile.operator_device_id)
      .maybeSingle<Device>();
    device = data ?? null;
  }

  return { ...result, device };
}
