import { requireUser } from "@/lib/supabase/auth";
import { FirstLoginForm } from "@/components/FirstLoginForm";
import { redirect } from "next/navigation";

export default async function PrimerIngresoPage() {
  const { profile } = await requireUser();

  // Ya lo hizo — no tiene sentido mostrarle el modal de nuevo.
  if (!profile.must_change_password) {
    redirect(profile.role === "admin" ? "/admin/equipos" : "/dashboard");
  }

  return (
    <main className="relative flex min-h-screen items-center justify-center overflow-hidden bg-bg px-4">
      <div className="absolute inset-0 flex items-center justify-center bg-black/55">
        <FirstLoginForm displayName={profile.display_name ?? profile.username} />
      </div>
    </main>
  );
}
