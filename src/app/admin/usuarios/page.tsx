import { requireAdmin } from "@/lib/supabase/auth";
import { CreateUserForm } from "@/components/admin/CreateUserForm";
import type { Profile } from "@/lib/supabase/types";

export default async function AdminUsuariosPage() {
  const { supabase } = await requireAdmin();

  const { data: users } = await supabase
    .from("profiles")
    .select("*")
    .eq("role", "usuario")
    .order("created_at", { ascending: false })
    .returns<Profile[]>();

  return (
    <div>
      <h1 className="mb-1 text-2xl font-bold tracking-tight">Usuarios</h1>
      <p className="mb-6 text-[13px] text-ink-muted">
        Los dueños de las máquinas. Vos creás la cuenta, ellos la activan en
        su primer ingreso.
      </p>

      <div className="grid grid-cols-[1.1fr_1.4fr] gap-6">
        <CreateUserForm />

        <div className="rounded-2xl border border-line bg-surface p-6">
          <div className="mb-4 text-sm font-bold">Todos los usuarios</div>
          <div className="space-y-2">
            {(users ?? []).length === 0 && (
              <p className="text-xs text-ink-faint">
                Todavía no creaste ningún usuario.
              </p>
            )}
            {(users ?? []).map((u) => (
              <div
                key={u.id}
                className="flex items-center justify-between rounded-lg border border-line px-4 py-3"
              >
                <div>
                  <div className="text-sm font-semibold">
                    {u.display_name ?? u.username}
                  </div>
                  <div className="text-xs text-ink-faint">@{u.username}</div>
                </div>
                <div className="text-right text-xs">
                  {u.must_change_password ? (
                    <span className="text-warn">Sin activar</span>
                  ) : (
                    <span className="text-ok">Activo</span>
                  )}
                </div>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
