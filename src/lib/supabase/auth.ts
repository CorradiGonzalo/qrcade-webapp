import "server-only";
import { redirect } from "next/navigation";
import { createClient } from "./server";
import type { Profile } from "./types";

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
