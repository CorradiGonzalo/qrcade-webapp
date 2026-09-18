import { headers } from "next/headers";
import Link from "next/link";
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

      <div className="mt-4 rounded-2xl border border-warn-line bg-warn-bg p-6">
        <div className="mb-1 text-sm font-bold text-warn">
          ⚠ Webhook de pagos — paso obligatorio y manual
        </div>
        <p className="mb-3 text-xs text-warn">
          Mercado Pago ya no deja cargar esto por código: tenés que pegar
          esta URL vos mismo en tu cuenta de Mercado Pago, una sola vez
          (Tus integraciones → tu app → Webhooks → evento &quot;Order&quot;).
          Sin este paso, tus máquinas van a mostrar el QR bien pero{" "}
          <strong>ningún pago se va a acreditar solo</strong>, tengan monto
          fijo o combos.
        </p>
        <div className="mb-3 select-all rounded-lg border border-line-soft bg-surface-2 px-3.5 py-3 font-mono text-[13px] text-ink">
          {webhookUrl}
        </div>
        <Link
          href="/dashboard/ayuda/webhook-mercadopago"
          className="inline-block rounded-lg bg-brand px-4 py-2.5 text-[13px] font-semibold text-white transition hover:opacity-90"
        >
          Ver instructivo paso a paso →
        </Link>
      </div>
    </div>
  );
}
