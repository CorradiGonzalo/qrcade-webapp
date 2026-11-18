import { requireOperator } from "@/lib/supabase/auth";
import { logoutAction } from "@/lib/actions/auth";
import { GiveFreeFichaButton } from "@/components/operador/GiveFreeFichaButton";

/**
 * Panel del operador — ver migración 0020. A propósito es la ÚNICA
 * pantalla que puede ver esta cuenta: ni sidebar, ni links a ningún
 * otro lado del panel, ni datos de la máquina más allá de su nombre.
 */
export default async function OperadorPage() {
  const { profile, device } = await requireOperator();

  return (
    <main className="flex min-h-screen flex-col items-center justify-center bg-bg px-4">
      <div className="w-full max-w-[420px] text-center">
        <p className="mb-1 text-sm text-ink-muted">
          Hola, {profile.display_name || profile.username}
        </p>

        {!profile.operator_active ? (
          <div className="mt-6 rounded-xl border border-[#6b2323] bg-danger-bg p-5 text-sm text-danger">
            Tu acceso fue desactivado. Hablá con el dueño de la máquina.
          </div>
        ) : !device || device.status === "revoked" ? (
          <div className="mt-6 rounded-xl border border-line-soft bg-surface-2 p-5 text-sm text-ink-muted">
            La máquina asociada a tu cuenta ya no está disponible.
          </div>
        ) : (
          <>
            <h1 className="mb-8 text-xl font-bold tracking-tight text-ink">
              {device.alias ?? device.mac}
            </h1>
            <GiveFreeFichaButton />
          </>
        )}

        <form action={logoutAction} className="mt-10">
          <button className="text-xs text-ink-faint hover:text-ink hover:underline">
            Cerrar sesión
          </button>
        </form>
      </div>
    </main>
  );
}
