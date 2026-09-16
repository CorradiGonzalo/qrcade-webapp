import Link from "next/link";
import { notFound } from "next/navigation";
import { requireActiveUser } from "@/lib/supabase/auth";
import { DeviceBillingForm } from "@/components/dashboard/DeviceBillingForm";
import type { Device, DeviceFichaCombo, Profile } from "@/lib/supabase/types";

export default async function CobroPage(
  props: PageProps<"/dashboard/maquinas/[id]/cobro">
) {
  const { id } = await props.params;
  const { user, supabase } = await requireActiveUser();

  const { data: device } = await supabase
    .from("devices")
    .select("*")
    .eq("id", id)
    .eq("owner_id", user.id)
    .single<Device>();

  if (!device) notFound();

  const { data: profile } = await supabase
    .from("profiles")
    .select("mp_access_token")
    .eq("id", user.id)
    .single<Pick<Profile, "mp_access_token">>();

  const { data: combos } = await supabase
    .from("device_ficha_combos")
    .select("*")
    .eq("device_id", device.id)
    .returns<DeviceFichaCombo[]>();

  return (
    <div className="mx-auto max-w-[760px]">
      <Link
        href={`/dashboard/maquinas/${device.id}`}
        className="mb-1.5 block text-xs text-ink-faint hover:text-ink"
      >
        ← {device.alias ?? device.mac}
      </Link>
      <h1 className="mb-1 text-2xl font-bold tracking-tight">
        Configuración de cobro
      </h1>
      <p className="mb-7 text-[13px] text-ink-muted">
        Estos datos arman la caja en tu cuenta de Mercado Pago y quedan
        cacheados acá — la ESP sólo dibuja el QR resultante.
      </p>

      <DeviceBillingForm
        device={device}
        combos={combos ?? []}
        hasAccountToken={Boolean(profile?.mp_access_token)}
      />
    </div>
  );
}
