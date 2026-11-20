import { requireOperator } from "@/lib/supabase/auth";
import { createAdminClient } from "@/lib/supabase/admin";
import { logoutAction } from "@/lib/actions/auth";
import { GiveFreeFichaButton } from "@/components/operador/GiveFreeFichaButton";
import { SalesPeriodFilter } from "@/components/dashboard/SalesPeriodFilter";
import { resolverRangoFechas } from "@/lib/sales-period";

/** Mismo tope que el detalle de máquina del dueño — ver esa página. */
const MAX_PAGOS = 500;

/**
 * Panel del operador — ver migración 0020. A propósito es la ÚNICA
 * pantalla que puede ver esta cuenta: ni sidebar, ni links a ningún
 * otro lado del panel, ni datos de la máquina más allá de su nombre.
 *
 * El contador de arriba (cafés vendidos / fichas gratis) usa el MISMO
 * filtro de período que la sección "Ventas" del dueño, pero nunca
 * muestra montos de dinero -- el operador no ve cobros, sólo cuántas
 * fichas salieron.
 */
export default async function OperadorPage(props: PageProps<"/operador">) {
  const { profile, device } = await requireOperator();
  const sp = await props.searchParams;

  const periodoParam = typeof sp.periodo === "string" ? sp.periodo : undefined;
  const desdeParam = typeof sp.desde === "string" ? sp.desde : undefined;
  const hastaParam = typeof sp.hasta === "string" ? sp.hasta : undefined;
  const rango = resolverRangoFechas({
    periodo: periodoParam,
    desde: desdeParam,
    hasta: hastaParam,
  });

  let totalCafesVendidos = 0;
  let totalFichasGratis = 0;

  // El operador no es dueño de la máquina, así que la RLS de
  // mp_payments/device_events lo bloquearía -- usamos admin client acá
  // igual que en requireOperator(), sólo para leer estos dos conteos.
  if (device && device.status !== "revoked" && profile.operator_active) {
    const admin = createAdminClient();

    let pagosQuery = admin
      .from("mp_payments")
      .select("fichas_dispensed")
      .eq("device_id", device.id)
      .limit(MAX_PAGOS);
    if (rango.desde) pagosQuery = pagosQuery.gte("created_at", rango.desde.toISOString());
    if (rango.hasta) pagosQuery = pagosQuery.lt("created_at", rango.hasta.toISOString());
    const { data: pagos } = await pagosQuery;
    totalCafesVendidos = (pagos ?? []).reduce(
      (acc, p) => acc + (p.fichas_dispensed ?? 0),
      0
    );

    let fichasGratisQuery = admin
      .from("device_events")
      .select("id", { count: "exact", head: true })
      .eq("device_id", device.id)
      .eq("type", "ficha_gratis");
    if (rango.desde) fichasGratisQuery = fichasGratisQuery.gte("created_at", rango.desde.toISOString());
    if (rango.hasta) fichasGratisQuery = fichasGratisQuery.lt("created_at", rango.hasta.toISOString());
    const { count } = await fichasGratisQuery;
    totalFichasGratis = count ?? 0;
  }

  return (
    <main className="flex min-h-screen flex-col items-center bg-bg px-4 py-10">
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
            <h1 className="mb-5 text-xl font-bold tracking-tight text-ink">
              {device.alias ?? device.mac}
            </h1>

            <div className="mb-6 rounded-2xl border border-line bg-surface p-[18px]">
              <div className="mb-3 flex flex-wrap items-center justify-between gap-2">
                <div className="text-[11px] text-ink-faint">{rango.label}</div>
              </div>

              <SalesPeriodFilter periodoActivo={rango.periodo} />

              <div className="flex justify-center gap-8">
                <div>
                  <div className="text-[11px] text-ink-faint">Cafés vendidos</div>
                  <div className="text-2xl font-bold text-ink">{totalCafesVendidos}</div>
                </div>
                <div>
                  <div className="text-[11px] text-ink-faint">Fichas gratis</div>
                  <div className="text-2xl font-bold text-ink">{totalFichasGratis}</div>
                </div>
              </div>
            </div>

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
