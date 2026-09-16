import { headers } from "next/headers";
import { requireActiveUser } from "@/lib/supabase/auth";
import { AccountTokenForm } from "@/components/dashboard/AccountTokenForm";
import type { Profile } from "@/lib/supabase/types";

export default async function CuentaPage() {
  const { user, supabase } = await requireActiveUser();
  const hdrs = await headers();
  const host = hdrs.get("host");
  const webhookUrl = host ? `https://${host}/api/mp/webhook` : "/api/mp/webhook";

  const { data: profile } = await supabase
    .from("profiles")
    .select("mp_access_token")
    .eq("id", user.id)
    .single<Pick<Profile, "mp_access_token">>();

  return (
    <div className="mx-auto max-w-[640px]">
      <h1 className="mb-1 text-2xl font-bold tracking-tight">Mi cuenta</h1>
      <p className="mb-7 text-[13px] text-ink-muted">
        Configuración de Mercado Pago compartida por todas tus máquinas.
      </p>

      <AccountTokenForm currentToken={profile?.mp_access_token ?? null} />

      <div className="mt-4 rounded-2xl border border-line bg-surface p-6">
        <div className="mb-1 text-sm font-bold">Webhook de pagos</div>
        <p className="mb-3 text-xs text-ink-muted">
          Pegá esta URL en tu cuenta de Mercado Pago para que los pagos se
          acrediten solos, sin que nadie tenga que revisar nada a mano.
        </p>
        <div className="select-all rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 font-mono text-[13px] text-ink">
          {webhookUrl}
        </div>
      </div>
    </div>
  );
}
