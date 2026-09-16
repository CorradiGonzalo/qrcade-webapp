"use server";

import { redirect } from "next/navigation";
import { createClient } from "@/lib/supabase/server";

export type LoginState = { error?: string } | undefined;

export async function loginAction(
  _prevState: LoginState,
  formData: FormData
): Promise<LoginState> {
  const username = String(formData.get("username") || "")
    .trim()
    .toLowerCase();
  const password = String(formData.get("password") || "");

  if (!username || !password) {
    return { error: "Completá usuario y contraseña." };
  }

  const supabase = await createClient();

  const { data: email, error: lookupError } = await supabase.rpc(
    "email_for_username",
    { p_username: username }
  );

  if (lookupError || !email) {
    return { error: "Usuario o contraseña incorrectos." };
  }

  const { error: signInError } = await supabase.auth.signInWithPassword({
    email,
    password,
  });

  if (signInError) {
    return { error: "Usuario o contraseña incorrectos." };
  }

  redirect("/dashboard");
}

export async function logoutAction() {
  const supabase = await createClient();
  await supabase.auth.signOut();
  redirect("/login");
}

export type ChangePasswordState = { error?: string } | undefined;

export async function changePasswordAction(
  _prevState: ChangePasswordState,
  formData: FormData
): Promise<ChangePasswordState> {
  const password = String(formData.get("password") || "");
  const confirm = String(formData.get("confirm") || "");
  const recoveryEmail = String(formData.get("recovery_email") || "").trim();

  if (password.length < 8) {
    return { error: "La contraseña tiene que tener al menos 8 caracteres." };
  }
  if (password !== confirm) {
    return { error: "Las contraseñas no coinciden." };
  }
  if (!recoveryEmail || !recoveryEmail.includes("@")) {
    return { error: "Ingresá un mail de recuperación válido." };
  }

  const supabase = await createClient();
  const {
    data: { user },
  } = await supabase.auth.getUser();

  if (!user) redirect("/login");

  const { error: updateAuthError } = await supabase.auth.updateUser({
    password,
  });
  if (updateAuthError) {
    return { error: "No se pudo actualizar la contraseña. Probá de nuevo." };
  }

  const { data: profile, error: updateProfileError } = await supabase
    .from("profiles")
    .update({ must_change_password: false, recovery_email: recoveryEmail })
    .eq("id", user.id)
    .select("role")
    .single();

  if (updateProfileError) {
    return { error: "No se pudo guardar el mail de recuperación." };
  }

  redirect(profile?.role === "admin" ? "/admin/equipos" : "/dashboard");
}
